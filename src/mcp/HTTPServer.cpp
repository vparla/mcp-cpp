//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: src/mcp/HTTPServer.cpp
// Purpose: HTTP/HTTPS JSON-RPC server using Boost.Beast (TLS 1.3 only for HTTPS)
//==========================================================================================================

#include <utility>
#include <thread>
#include <atomic>
#include <sstream>
#include <algorithm>
#include <cctype>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

#include "logging/Logger.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/HTTPServer.hpp"

#include <openssl/ssl.h>

namespace mcp {
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

class HTTPServer::Impl {
public:
    HTTPServer::Options opts;
    std::atomic<bool> running{false};

    net::io_context ioc;
    std::unique_ptr<tcp::acceptor> acceptor;
    std::unique_ptr<ssl::context> sslCtx; // present when scheme==https
    std::thread ioThread;

    ITransport::RequestHandler requestHandler;
    ITransport::NotificationHandler notificationHandler;
    ITransport::ErrorHandler errorHandler;

    explicit Impl(const HTTPServer::Options& o) : opts(o) {
        if (opts.scheme == "https") {
            sslCtx = std::make_unique<ssl::context>(ssl::context::tls_server);
            // TLS 1.3 only
            ::SSL_CTX_set_min_proto_version(sslCtx->native_handle(), TLS1_3_VERSION);
            ::SSL_CTX_set_max_proto_version(sslCtx->native_handle(), TLS1_3_VERSION);
            try {
                sslCtx->use_certificate_chain_file(opts.certFile);
                sslCtx->use_private_key_file(opts.keyFile, ssl::context::file_format::pem);
            } catch (const std::exception& e) {
                LOG_ERROR("HTTPServer: failed to load certificate/key: {}", e.what());
                throw;
            }
            sslCtx->set_options(
                ssl::context::default_workarounds | ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1 | ssl::context::no_tlsv1_2);
        }
    }

    ~Impl() {
        if (ioThread.joinable()) {
            ioc.stop();
            ioThread.join();
        }
    }

    void setError(const std::string& msg) {
        if (errorHandler) { errorHandler(msg); }
    }

    net::awaitable<void> session_plain(tcp::socket socket) {
        try {
            boost::beast::tcp_stream stream(std::move(socket));
            boost::beast::flat_buffer buffer;
            for (;;) {
                http::request<http::string_body> req;
                co_await http::async_read(stream, buffer, req, net::use_awaitable);
                auto res = makeResponse(req);
                co_await http::async_write(stream, res, net::use_awaitable);
                break; // close after single request
            }
            boost::system::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_send, ec);
        } catch (const boost::system::system_error& e) {
            if (!running.load()) {
                // Suppress shutdown-related errors; log at DEBUG only in debug builds
#ifdef _DEBUG
                LOG_DEBUG("HTTPServer plain session suppressed during shutdown: {}", e.what());
#endif
            } else {
                setError(std::string("HTTPServer plain session error: ") + e.what());
            }
        } catch (const std::exception& e) {
            if (!running.load()) {
                // Suppress shutdown-related errors; log at DEBUG only in debug builds
#ifdef _DEBUG
                LOG_DEBUG("HTTPServer plain session suppressed during shutdown: {}", e.what());
#endif
            } else {
                setError(std::string("HTTPServer plain session error: ") + e.what());
            }
        }
        co_return;
    }

    net::awaitable<void> session_tls(tcp::socket socket) {
        try {
            ssl::stream<tcp::socket> tls(std::move(socket), *sslCtx);
            co_await tls.async_handshake(ssl::stream_base::server, net::use_awaitable);
            boost::beast::flat_buffer buffer;
            for (;;) {
                http::request<http::string_body> req;
                co_await http::async_read(tls, buffer, req, net::use_awaitable);
                auto res = makeResponse(req);
                co_await http::async_write(tls, res, net::use_awaitable);
                break; // close after single request
            }
            boost::system::error_code ec;
            tls.shutdown(ec);
        } catch (const boost::system::system_error& e) {
            if (!running.load()) {
                // Suppress shutdown-related errors; log at DEBUG only in debug builds
                #ifdef _DEBUG
                LOG_DEBUG("HTTPServer TLS session suppressed during shutdown: {}", e.what());
                #endif
            } else {
                setError(std::string("HTTPServer TLS session error: ") + e.what());
            }
        } catch (const std::exception& e) {
            if (!running.load()) {
                // Suppress shutdown-related errors; log at DEBUG only in debug builds
                #ifdef _DEBUG
                LOG_DEBUG("HTTPServer TLS session suppressed during shutdown: {}", e.what());
                #endif
            } else {
                setError(std::string("HTTPServer TLS session error: ") + e.what());
            }
        }
        co_return;
    }

    http::response<http::string_body> makeResponse(const http::request<http::string_body>& req) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.keep_alive(false);

        if (req.method() != http::verb::post) {
            res.result(http::status::bad_request);
            res.body() = std::string("{\"error\":\"POST required\"}");
            res.prepare_payload();
            return res;
        }

        const std::string target = std::string(req.target());
        if (target == opts.rpcPath) {
            JSONRPCRequest rpc;
            if (!rpc.Deserialize(req.body())) {
                auto err = CreateErrorResponse(nullptr, JSONRPCErrorCodes::ParseError, "Parse error");
                res.body() = err->Serialize(); res.prepare_payload();
                return res;
            }
            std::unique_ptr<JSONRPCResponse> out;
            try {
                out = requestHandler ? requestHandler(rpc) : nullptr;
            } catch (const std::exception& e) {
                auto er = std::make_unique<JSONRPCResponse>(); er->id = rpc.id;
                er->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what());
                out = std::move(er);
            }
            if (!out) {
                auto er = std::make_unique<JSONRPCResponse>(); er->id = rpc.id;
                er->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "No response from handler");
                out = std::move(er);
            }
            res.body() = out->Serialize(); res.prepare_payload();
            return res;
        }

        if (target == opts.notifyPath) {
            JSONRPCNotification note;
            if (!note.Deserialize(req.body())) {
                auto er = CreateErrorResponse(nullptr, JSONRPCErrorCodes::InvalidRequest, "Invalid notification");
                res.body() = er->Serialize(); res.prepare_payload();
                return res;
            }
            if (notificationHandler) {
                try { notificationHandler(std::make_unique<JSONRPCNotification>(std::move(note))); }
                catch (const std::exception& e) { setError(std::string("Notification handler error: ") + e.what()); }
            }
            res.body() = std::string("{}"); res.prepare_payload();
            return res;
        }

        res.result(http::status::not_found);
        res.body() = std::string("{\"error\":\"Not found\"}");
        res.prepare_payload();
        return res;
    }

    net::awaitable<void> acceptLoop() {
        try {
            // Validate port strictly: numeric and within [0, 65535]
            if (opts.port.empty()) {
                setError("HTTPServer invalid port: empty");
                co_return;
            }
            bool allDigits = std::all_of(opts.port.begin(), opts.port.end(), [](unsigned char ch){ return std::isdigit(ch) != 0; });
            if (!allDigits) {
                setError(std::string("HTTPServer invalid port (non-numeric): ") + opts.port);
                co_return;
            }
            unsigned long portNum = 0;
            try {
                portNum = std::stoul(opts.port);
            } catch (...) {
                setError(std::string("HTTPServer invalid port (parse failure): ") + opts.port);
                co_return;
            }
            if (portNum > 65535ul) {
                setError(std::string("HTTPServer invalid port (out of range): ") + opts.port);
                co_return;
            }
            tcp::resolver resolver(co_await net::this_coro::executor);
            auto r = resolver.resolve(opts.address, opts.port);
            tcp::endpoint ep = *r.begin();

            acceptor = std::make_unique<tcp::acceptor>(ioc);
            acceptor->open(ep.protocol());
            acceptor->set_option(tcp::acceptor::reuse_address(true));
            acceptor->bind(ep);
            acceptor->listen();

            while (running.load()) {
                tcp::socket socket = co_await acceptor->async_accept(net::use_awaitable);
                if (opts.scheme == "https") {
                    net::co_spawn(ioc, session_tls(std::move(socket)), net::detached);
                } else {
                    net::co_spawn(ioc, session_plain(std::move(socket)), net::detached);
                }
            }
        } catch (const boost::system::system_error& e) {
            if (!running.load()) {
                // Suppress shutdown-related errors (e.g., operation_aborted when acceptor is closed); log at DEBUG only in debug builds
                #ifdef _DEBUG
                LOG_DEBUG("HTTPServer accept suppressed during shutdown: {}", e.what());
                #endif
            } else {
                setError(std::string("HTTPServer accept error: ") + e.what());
            }
        } catch (const std::exception& e) {
            if (!running.load()) {
                // Suppress shutdown-related errors; log at DEBUG only in debug builds
                #ifdef _DEBUG
                LOG_DEBUG("HTTPServer accept suppressed during shutdown: {}", e.what());
                #endif
            } else {
                setError(std::string("HTTPServer accept error: ") + e.what());
            }
        }
        co_return;
    }
};

HTTPServer::HTTPServer(const Options& opts)
    : pImpl(std::make_unique<Impl>(opts)) {}

HTTPServer::~HTTPServer() = default;

std::future<void> HTTPServer::Start() {
    std::promise<void> ready; auto fut = ready.get_future();
    pImpl->running.store(true);
    pImpl->ioThread = std::thread([this, pr = std::move(ready)]() mutable {
        try {
            net::co_spawn(pImpl->ioc, pImpl->acceptLoop(), net::detached);
            pr.set_value();
            pImpl->ioc.run();
        } catch (const std::exception& e) {
            if (pImpl->errorHandler) { pImpl->errorHandler(e.what()); }
            pr.set_value();
        }
    });
    return fut;
}

std::future<void> HTTPServer::Stop() {
    std::promise<void> done; auto fut = done.get_future();
    try {
        pImpl->running.store(false);
        if (pImpl->acceptor) {
            boost::system::error_code ec; pImpl->acceptor->close(ec);
        }
        pImpl->ioc.stop();
        if (pImpl->ioThread.joinable()) {
            pImpl->ioThread.join();
        }
    } catch (...) {}
    done.set_value();
    return fut;
}

void HTTPServer::SetRequestHandler(ITransport::RequestHandler handler) { 
    pImpl->requestHandler = std::move(handler);
}
void HTTPServer::SetNotificationHandler(ITransport::NotificationHandler handler) { 
    pImpl->notificationHandler = std::move(handler);
}
  
void HTTPServer::SetErrorHandler(ITransport::ErrorHandler handler) {
      pImpl->errorHandler = std::move(handler);
 }

std::unique_ptr<ITransportAcceptor> HTTPServerFactory::CreateTransportAcceptor(const std::string& config) {
    HTTPServer::Options opts;
    // Factory default: http if scheme omitted
    opts.scheme = "http";

    std::string cfg = config;
    // Trim leading/trailing spaces
    auto trim = [](std::string& s){
        auto notSpace = [](unsigned char c){ return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    };
    trim(cfg);

    // Detect scheme
    auto startsWith = [](const std::string& s, const char* pfx){ return s.rfind(pfx, 0) == 0; };
    if (startsWith(cfg, "http://")) {
        opts.scheme = "http";
        cfg = cfg.substr(7);
    } else if (startsWith(cfg, "https://")) {
        opts.scheme = "https";
        cfg = cfg.substr(8);
    }

    // Split query params
    std::string hostPortPath = cfg;
    std::string query;
    auto qpos = cfg.find('?');
    if (qpos != std::string::npos) {
        hostPortPath = cfg.substr(0, qpos);
        query = cfg.substr(qpos + 1);
    }

    // Strip path component if present
    std::string hostPort = hostPortPath;
    auto slash = hostPortPath.find('/');
    if (slash != std::string::npos) {
        hostPort = hostPortPath.substr(0, slash);
    }
    trim(hostPort);

    // Parse host[:port] including IPv4/IPv6 in [addr]:port form
    if (!hostPort.empty()) {
        if (hostPort.front() == '[') {
            auto rb = hostPort.find(']');
            if (rb != std::string::npos) {
                opts.address = hostPort.substr(1, rb - 1);
                if (rb + 1 < hostPort.size() && hostPort[rb + 1] == ':') {
                    opts.port = hostPort.substr(rb + 2);
                }
            }
        } else {
            auto colon = hostPort.rfind(':');
            if (colon != std::string::npos) {
                opts.address = hostPort.substr(0, colon);
                opts.port = hostPort.substr(colon + 1);
            } else {
                opts.address = hostPort;
            }
        }
        trim(opts.address);
        trim(opts.port);
        if (opts.port.empty()) opts.port = "9443"; // default
    }

    // Parse query parameters: cert, key
    if (!query.empty()) {
        std::stringstream ss(query);
        std::string kv;
        while (std::getline(ss, kv, '&')) {
            auto eq = kv.find('=');
            std::string key = (eq == std::string::npos) ? kv : kv.substr(0, eq);
            std::string val = (eq == std::string::npos) ? std::string() : kv.substr(eq + 1);
            if (key == "cert") opts.certFile = val;
            else if (key == "key") opts.keyFile = val;
        }
    }

    // Return server acceptor
    return std::unique_ptr<ITransportAcceptor>(new HTTPServer(opts));
}

} // namespace mcp
