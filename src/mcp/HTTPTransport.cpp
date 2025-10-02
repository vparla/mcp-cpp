//==========================================================================================================
// SPDX-License-Identifier: MIT 
// Copyright (c) 2025 Vinny Parla
// File: src/mcp/HTTPTransport.cpp
// Purpose: HTTP/HTTPS JSON-RPC client transport using Boost.Beast (TLS 1.3 only for HTTPS)
//==========================================================================================================

//==========================================================================================================
#include <utility>
#include <thread>
#include <atomic>
#include <sstream>
#include <random>
#include <mutex>
#include <unordered_map>

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
#include "mcp/Transport.h"
#include "mcp/HTTPTransport.hpp"

#include <openssl/ssl.h>

namespace mcp {
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

class HTTPTransport::Impl {
public:
    HTTPTransport::Options opts;
    std::string sessionId;
    std::atomic<bool> connected{false};

    net::io_context ioc;
    std::thread ioThread;
    std::unique_ptr<ssl::context> sslCtx; // present when https
    std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>> workGuard;

    HTTPTransport::ErrorHandler errorHandler;
    HTTPTransport::NotificationHandler notificationHandler; // not used in v1
    HTTPTransport::RequestHandler requestHandler;           // not used client-side
    std::atomic<unsigned int> requestCounter{0u};
    std::mutex requestMutex;
    std::unordered_map<std::string, std::promise<std::unique_ptr<JSONRPCResponse>>> pendingRequests;
    bool caInitOk{true};

    explicit Impl(const HTTPTransport::Options& o) : opts(o) {
        // Random session id similar to InMemoryTransport
        std::random_device rd; std::mt19937 gen(rd()); std::uniform_int_distribution<> dis(1000, 9999);
        sessionId = "http-" + std::to_string(dis(gen));
        if (opts.scheme == "https") {
            sslCtx = std::make_unique<ssl::context>(ssl::context::tls_client);
            ::SSL_CTX_set_min_proto_version(sslCtx->native_handle(), TLS1_3_VERSION);
            ::SSL_CTX_set_max_proto_version(sslCtx->native_handle(), TLS1_3_VERSION);
            ::ERR_clear_error();
            const bool userProvidedCA = !opts.caFile.empty() || !opts.caPath.empty();
            if (userProvidedCA) {
                try {
                    if (!opts.caFile.empty()) { sslCtx->load_verify_file(opts.caFile); }
                    if (!opts.caPath.empty()) { sslCtx->add_verify_path(opts.caPath); }
                } catch (...) {
                    setError("HTTPS: failed to load user-provided CA file/path");
                    caInitOk = false;
                }
            } else {
                try { sslCtx->set_default_verify_paths(); } catch (...) {}
            }
            sslCtx->set_verify_mode(ssl::verify_peer);
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

    std::string generateRequestId() { return std::string("http-req-") + std::to_string(++requestCounter); }

    // Coroutine: POST JSON and return response body
    net::awaitable<std::string> coPostJson(const std::string path, const std::string body) {
        try {
            tcp::resolver resolver(co_await net::this_coro::executor);
            auto results = co_await resolver.async_resolve(opts.host, opts.port, net::use_awaitable);

            if (opts.scheme == "https") {
                if (!caInitOk) {
                    setError("HTTPS: CA initialization failed (bad caFile/caPath)");
                    co_return std::string();
                }
                boost::beast::ssl_stream<boost::beast::tcp_stream> stream(co_await net::this_coro::executor, *sslCtx);
                if (!opts.serverName.empty()) {
                    if (!::SSL_set_tlsext_host_name(stream.native_handle(), opts.serverName.c_str())) {
                        setError("HTTPS: failed to set SNI hostname");
                    }
                    (void)::SSL_set1_host(stream.native_handle(), opts.serverName.c_str());
                }
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                co_await stream.next_layer().async_connect(results, net::use_awaitable);
                co_await stream.async_handshake(ssl::stream_base::client, net::use_awaitable);

                http::request<http::string_body> req{http::verb::post, path, 11};
                req.set(http::field::host, opts.serverName.empty() ? opts.host : opts.serverName);
                req.set(http::field::content_type, "application/json");
                req.set(http::field::accept, "application/json");
                req.set(http::field::connection, "close");
                req.body() = body;
                req.prepare_payload();

                // Set timeouts
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
                co_await http::async_write(stream, req, net::use_awaitable);
                boost::beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, net::use_awaitable);
                boost::system::error_code ec; stream.shutdown(ec);
                co_return res.body();
            } else {
                boost::beast::tcp_stream stream(co_await net::this_coro::executor);
                stream.expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                co_await stream.async_connect(results, net::use_awaitable);

                http::request<http::string_body> req{http::verb::post, path, 11};
                req.set(http::field::host, opts.host);
                req.set(http::field::content_type, "application/json");
                req.set(http::field::accept, "application/json");
                req.set(http::field::connection, "close");
                req.body() = body;
                req.prepare_payload();

                stream.expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
                co_await http::async_write(stream, req, net::use_awaitable);
                boost::beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, net::use_awaitable);
                boost::system::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
                co_return res.body();
            }
        } catch (const std::exception& e) {
            setError(std::string("HTTP coPostJson failed: ") + e.what());
        }
        co_return std::string();
    }
};

HTTPTransport::HTTPTransport(const Options& opts)
    : pImpl(std::make_unique<Impl>(opts)) {}

HTTPTransport::~HTTPTransport() = default;

std::future<void> HTTPTransport::Start() {
    FUNC_SCOPE();
    std::promise<void> ready; auto fut = ready.get_future();
    pImpl->connected.store(true);
    pImpl->workGuard = std::make_unique<net::executor_work_guard<net::io_context::executor_type>>(net::make_work_guard(pImpl->ioc));
    pImpl->ioThread = std::thread([this, pr = std::move(ready)]() mutable {
        try {
            pr.set_value();
            pImpl->ioc.run();
        } catch (const std::exception& e) {
            if (pImpl->errorHandler) { pImpl->errorHandler(e.what()); }
            pr.set_value();
        }
    });
    return fut;
}

std::future<void> HTTPTransport::Close() {
    FUNC_SCOPE();
    std::promise<void> done; auto fut = done.get_future();
    try {
        pImpl->connected.store(false);
        if (pImpl->workGuard) { pImpl->workGuard->reset(); pImpl->workGuard.reset(); }
        pImpl->ioc.stop();
        if (pImpl->ioThread.joinable()) pImpl->ioThread.join();
        // Fail any pending requests like InMemoryTransport
        {
            std::lock_guard<std::mutex> lk(pImpl->requestMutex);
            for (auto& kv : pImpl->pendingRequests) {
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = kv.first;
                resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport closed", std::nullopt);
                kv.second.set_value(std::move(resp));
            }
            pImpl->pendingRequests.clear();
        }
    } catch (...) {}
    done.set_value();
    return fut;
}

bool HTTPTransport::IsConnected() const {
    FUNC_SCOPE(); return pImpl->connected.load();
}
std::string HTTPTransport::GetSessionId() const {
    FUNC_SCOPE(); return pImpl->sessionId;
}

std::future<std::unique_ptr<JSONRPCResponse>> HTTPTransport::SendRequest(
    std::unique_ptr<JSONRPCRequest> request) {
    FUNC_SCOPE();
    // Promise stored in map to match InMemoryTransport pattern
    std::promise<std::unique_ptr<JSONRPCResponse>> promise;
    auto fut = promise.get_future();

    if (!pImpl->connected.load()) {
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = request ? request->id : nullptr;
        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport not connected", std::nullopt);
        promise.set_value(std::move(resp));
        return fut;
    }

    // Ensure request id present (mirror InMemoryTransport behavior)
    if (request) {
        bool callerSetId = false; std::string idStr;
        std::visit([&](auto&& idVal){
            using T = std::decay_t<decltype(idVal)>;
            if constexpr (std::is_same_v<T, std::string>) { if (!idVal.empty()) { callerSetId = true; } }
            else if constexpr (std::is_same_v<T, int64_t>) { callerSetId = true; }
        }, request->id);
        if (!callerSetId) {
            const std::string newId = pImpl->generateRequestId();
            request->id = newId;
        }
    }
    std::string payload = request ? request->Serialize() : std::string();
    // Compute id string for map key
    std::string idStr;
    if (request) {
        std::visit([&](const auto& id){
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, std::string>) { idStr = id; }
            else if constexpr (std::is_same_v<T, int64_t>) { idStr = std::to_string(id); }
        }, request->id);
    }
    {
        std::lock_guard<std::mutex> lk(pImpl->requestMutex);
        pImpl->pendingRequests[idStr] = std::move(promise);
    }

    net::co_spawn(pImpl->ioc, pImpl->coPostJson(pImpl->opts.rpcPath, payload),
        [this, idStr](std::exception_ptr eptr, std::string body) mutable {
            if (eptr) {
                try { std::rethrow_exception(eptr); } catch (const std::exception& e) { if (pImpl->errorHandler) { pImpl->errorHandler(e.what()); } }
            }
            std::unique_ptr<JSONRPCResponse> out;
            if (!body.empty()) {
                auto resp = std::make_unique<JSONRPCResponse>();
                if (resp->Deserialize(body)) {
                    out = std::move(resp);
                }
            }
            if (!out) {
                auto err = std::make_unique<JSONRPCResponse>();
                // Attempt to set id from known idStr if available
                if (!idStr.empty()) {
                    err->id = idStr;
                } else {
                    err->id = nullptr;
                }
                err->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Invalid/empty HTTP response", std::nullopt);
                out = std::move(err);
            }
            // Deliver to pending promise
            std::lock_guard<std::mutex> lk(pImpl->requestMutex);
            auto it = pImpl->pendingRequests.find(idStr);
            if (it != pImpl->pendingRequests.end()) {
                it->second.set_value(std::move(out));
                pImpl->pendingRequests.erase(it);
            }
        });

    return fut;
}

std::future<void> HTTPTransport::SendNotification(
    std::unique_ptr<JSONRPCNotification> notification) {
    FUNC_SCOPE();
    std::promise<void> done; auto fut = done.get_future();
    if (!pImpl->connected.load()) { done.set_value(); return fut; }
    std::string payload = notification ? notification->Serialize() : std::string();

    net::co_spawn(pImpl->ioc, pImpl->coPostJson(pImpl->opts.notifyPath, payload),
        [this, pr = std::move(done)](std::exception_ptr eptr, std::string /*body*/) mutable {
            if (eptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& e) {
                    if (pImpl->errorHandler) {
                        pImpl->errorHandler(e.what());
                    }
                }
            }
            pr.set_value();
        });

    return fut;
}

void HTTPTransport::SetNotificationHandler(NotificationHandler handler) {
    FUNC_SCOPE();
    // Not used in HTTP v1 (no server->client push). Reserved for polling/SSE variants.
    pImpl->notificationHandler = std::move(handler);
}

void HTTPTransport::SetRequestHandler(RequestHandler handler) {
    FUNC_SCOPE();
    // Client does not accept inbound requests over HTTP v1.
    pImpl->requestHandler = std::move(handler);
}

void HTTPTransport::SetErrorHandler(ErrorHandler handler) {
    FUNC_SCOPE();
    pImpl->errorHandler = std::move(handler);
}

} // namespace mcp
