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

    HTTPServer::RequestHandler requestHandler;
    HTTPServer::NotificationHandler notificationHandler;
    HTTPServer::ErrorHandler errorHandler;

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

void HTTPServer::SetRequestHandler(RequestHandler handler) { 
    pImpl->requestHandler = std::move(handler);
}
void HTTPServer::SetNotificationHandler(NotificationHandler handler) { 
    pImpl->notificationHandler = std::move(handler);
}
void HTTPServer::SetErrorHandler(ErrorHandler handler) {
    pImpl->errorHandler = std::move(handler);
}

} // namespace mcp
