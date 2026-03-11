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
#include <chrono>
#include <future>
#include <string>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

#include "logging/Logger.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/JsonRpcMessageRouter.h"
#include "mcp/Protocol.h"
#include "mcp/Transport.h"
#include "mcp/HTTPTransport.hpp"
#include "mcp/auth/IAuth.hpp"
#include "mcp/auth/BearerAuth.hpp"
#include "mcp/auth/OAuth2ClientCredentialsAuth.hpp"

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace mcp {
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using tcp = net::ip::tcp;

class HTTPTransport::Impl {
public:
    struct HttpExchangeResult {
        int status{0};
        std::string contentType;
        std::string body;
        std::vector<mcp::auth::HeaderKV> headers;
        std::string sessionId;
    };

    HTTPTransport::Options opts;
    std::string sessionId;
    std::string mcpSessionId;
    std::string negotiatedProtocolVersion{PROTOCOL_VERSION};
    std::atomic<bool> connected{false};
    std::atomic<bool> closing{false};
    std::atomic<bool> sseEnabled{true};
    std::atomic<bool> sseRunning{false};

    net::io_context ioc;
    std::thread ioThread;
    std::thread sseThread;
    std::unique_ptr<ssl::context> sslCtx; // present when https
    std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>> workGuard;

    HTTPTransport::ErrorHandler errorHandler;
    HTTPTransport::NotificationHandler notificationHandler; // not used in v1
    HTTPTransport::RequestHandler requestHandler;           // not used client-side
    std::atomic<unsigned int> requestCounter{0u};
    std::mutex requestMutex;
    std::unordered_map<std::string, std::promise<std::unique_ptr<JSONRPCResponse>>> pendingRequests;
    bool caInitOk{true};
    std::unique_ptr<IJsonRpcMessageRouter> router;
    std::mutex sessionMutex;

    // Pluggable auth
    std::shared_ptr<mcp::auth::IAuth> authStrong;
    mcp::auth::IAuth* authWeak{nullptr};

    // Snapshot of the last HTTP response (status + headers) for auth discovery
    mutable std::mutex lastRespMutex;
    HTTPTransport::HttpResponseInfo lastResp;

    explicit Impl(const HTTPTransport::Options& o) : opts(o) {
        // Random session id similar to InMemoryTransport
        std::random_device rd; std::mt19937 gen(rd()); std::uniform_int_distribution<> dis(1000, 9999);
        sessionId = "http-" + std::to_string(dis(gen));
        router = MakeDefaultJsonRpcMessageRouter();
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
                try {
                    sslCtx->set_default_verify_paths();
                } catch (const std::exception& e) {
                    LOG_DEBUG("HTTPS: set_default_verify_paths failed: {}", e.what());
                } catch (...) {
                    LOG_DEBUG("HTTPS: set_default_verify_paths failed with unknown exception");
                }
            }
            sslCtx->set_verify_mode(ssl::verify_peer);
        }

        // Construct auth based on options (back-compat with string config)
        try {
            if (opts.auth == std::string("bearer")) {
                if (!opts.bearerToken.empty()) {
                    authStrong = std::make_shared<mcp::auth::BearerAuth>(opts.bearerToken);
                }
            } else if (opts.auth == std::string("oauth2")) {
                authStrong = std::make_shared<mcp::auth::OAuth2ClientCredentialsAuth>(
                    opts.oauthTokenUrl,
                    opts.clientId,
                    opts.clientSecret,
                    opts.scope,
                    opts.tokenRefreshSkewSeconds,
                    opts.connectTimeoutMs,
                    opts.readTimeoutMs,
                    opts.caFile,
                    opts.caPath);
            }
        } catch (...) {
            // best-effort; errors surface during ensureReady()
            LOG_DEBUG("HTTPTransport: auth initialization failed (suppressed; will surface in ensureReady)");
        }
    }

    ~Impl() {
        closing.store(true);
        stopSseLoop();
        if (ioThread.joinable()) {
            ioc.stop();
            ioThread.join();
        }
    }

    bool useStreamableHttp() const {
        return !opts.endpointPath.empty();
    }

    std::string requestPath() const {
        return useStreamableHttp() ? opts.endpointPath : opts.rpcPath;
    }

    std::string notifyPath() const {
        return useStreamableHttp() ? opts.endpointPath : opts.notifyPath;
    }

    std::string streamPath() const {
        if (!opts.streamPath.empty()) {
            return opts.streamPath;
        }
        return useStreamableHttp() ? opts.endpointPath : std::string();
    }

    void recordResponse(const http::response<http::string_body>& res) {
        HTTPTransport::HttpResponseInfo info;
        info.status = static_cast<int>(res.result_int());
        for (const auto& h : res.base()) {
            mcp::auth::HeaderKV kv;
            kv.name = std::string(h.name_string());
            kv.value = std::string(h.value());
            info.headers.push_back(std::move(kv));
        }
        auto const& wa = res[http::field::www_authenticate];
        if (!wa.empty()) {
            info.wwwAuthenticate = std::string(wa);
        }
        {
            std::lock_guard<std::mutex> lk(lastRespMutex);
            lastResp = std::move(info);
        }
    }

    HttpExchangeResult makeExchangeResult(const http::response<http::string_body>& res) {
        HttpExchangeResult out;
        out.status = static_cast<int>(res.result_int());
        out.body = res.body();
        out.contentType = std::string(res[http::field::content_type]);
        for (const auto& h : res.base()) {
            mcp::auth::HeaderKV kv;
            kv.name = std::string(h.name_string());
            kv.value = std::string(h.value());
            if (kv.name == std::string("Mcp-Session-Id")) {
                out.sessionId = kv.value;
            }
            out.headers.push_back(std::move(kv));
        }
        recordResponse(res);
        return out;
    }

    void setError(const std::string& msg) {
        if (errorHandler) {
            errorHandler(msg);
        }
    }

    void stopSseLoop() {
        sseEnabled.store(false);
        if (sseThread.joinable()) {
            sseThread.join();
        }
        sseRunning.store(false);
    }

    void maybeCaptureSessionId(const HttpExchangeResult& exchange) {
        if (exchange.sessionId.empty()) {
            return;
        }
        std::lock_guard<std::mutex> lk(sessionMutex);
        mcpSessionId = exchange.sessionId;
    }

    void maybeStartSseLoop();
    void runSseLoop();
    void handleIncomingJson(const std::string& json, const std::string& fallbackId = std::string());

    void deliverInternalError(const std::string& id, const std::string& message) {
        std::promise<std::unique_ptr<JSONRPCResponse>> deliverPromise;
        bool havePromise = false;
        {
            std::lock_guard<std::mutex> lk(requestMutex);
            auto it = pendingRequests.find(id);
            if (it != pendingRequests.end()) {
                deliverPromise = std::move(it->second);
                pendingRequests.erase(it);
                havePromise = true;
            }
        }
        if (havePromise) {
            auto err = std::make_unique<JSONRPCResponse>();
            err->id = id;
            err->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, message, std::nullopt);
            deliverPromise.set_value(std::move(err));
        }
    }

    bool deliverResponse(JSONRPCResponse&& response, const std::string& fallbackId) {
        std::string respKey;
        std::visit([&](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, std::string>) {
                respKey = id;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                respKey = std::to_string(id);
            }
        }, response.id);

        std::promise<std::unique_ptr<JSONRPCResponse>> deliverPromise;
        bool havePromise = false;
        {
            std::lock_guard<std::mutex> lk(requestMutex);
            auto it = !respKey.empty() ? pendingRequests.find(respKey) : pendingRequests.end();
            if (it != pendingRequests.end()) {
                deliverPromise = std::move(it->second);
                pendingRequests.erase(it);
                havePromise = true;
            }
        }
        if (havePromise) {
            deliverPromise.set_value(std::make_unique<JSONRPCResponse>(std::move(response)));
            return true;
        }
        if (!fallbackId.empty()) {
            deliverInternalError(fallbackId, "Mismatched response id");
            return true;
        }
        return false;
    }

    std::string generateRequestId() { 
        return std::string("http-req-") + std::to_string(++requestCounter); 
    }
    
    // Coroutine: apply auth headers via IAuth
    net::awaitable<void> coEnsureAuthHeader(http::request<http::string_body>& req) {
        mcp::auth::IAuth* a = authStrong ? authStrong.get() : authWeak;
        if (a) {
            co_await a->ensureReady();
            auto hs = a->headers();
            for (const auto& h : hs) {
                req.set(h.name, h.value);
            }
        }
        co_return;
    }

    // Coroutine: POST JSON and return the HTTP exchange result
    net::awaitable<HttpExchangeResult> coPostJson(std::string path, std::string body) {
        HttpExchangeResult exchange;
        try {
            http::request<http::string_body> req{http::verb::post, path, 11};
            req.set(http::field::content_type, "application/json");
            req.set(http::field::accept, useStreamableHttp() ? "application/json, text/event-stream" : "application/json");
            req.set(http::field::connection, "close");
            req.body() = std::move(body);
            req.prepare_payload();
            {
                std::lock_guard<std::mutex> lk(sessionMutex);
                if (useStreamableHttp() && !mcpSessionId.empty()) {
                    req.set("Mcp-Session-Id", mcpSessionId);
                }
                if (useStreamableHttp() && !negotiatedProtocolVersion.empty()) {
                    req.set("MCP-Protocol-Version", negotiatedProtocolVersion);
                }
            }
            co_await coEnsureAuthHeader(req);

            auto ex = co_await net::this_coro::executor;
            tcp::resolver resolver(ex);
            auto results = co_await resolver.async_resolve(opts.host, opts.port, net::use_awaitable);

            if (opts.scheme == "https") {
                if (!caInitOk) {
                    setError("HTTPS: CA initialization failed (bad caFile/caPath)");
                    co_return exchange;
                }
                boost::beast::ssl_stream<boost::beast::tcp_stream> stream(ex, *sslCtx);
                if (!opts.serverName.empty()) {
                    if (!::SSL_set_tlsext_host_name(stream.native_handle(), opts.serverName.c_str())) {
                        setError("HTTPS: failed to set SNI hostname");
                    }
                    (void)::SSL_set1_host(stream.native_handle(), opts.serverName.c_str());
                }
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                co_await stream.next_layer().async_connect(results, net::use_awaitable);
                co_await stream.async_handshake(ssl::stream_base::client, net::use_awaitable);
                req.set(http::field::host, opts.serverName.empty() ? opts.host : opts.serverName);
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
                co_await http::async_write(stream, req, net::use_awaitable);
                boost::beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, net::use_awaitable);
                exchange = makeExchangeResult(res);
                boost::system::error_code ec;
                stream.shutdown(ec);
                co_return exchange;
            }

            boost::beast::tcp_stream stream(ex);
            stream.expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
            co_await stream.async_connect(results, net::use_awaitable);
            req.set(http::field::host, opts.host);
            stream.expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
            co_await http::async_write(stream, req, net::use_awaitable);
            boost::beast::flat_buffer buffer;
            http::response<http::string_body> res;
            co_await http::async_read(stream, buffer, res, net::use_awaitable);
            exchange = makeExchangeResult(res);
            boost::system::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            co_return exchange;
        } catch (const std::exception& e) {
            setError(std::string("HTTP coPostJson failed: ") + e.what());
        }
        co_return exchange;
    }

    net::awaitable<HttpExchangeResult> coDeleteSession() {
        HttpExchangeResult exchange;
        if (!useStreamableHttp() || !opts.enableDeleteOnClose) {
            co_return exchange;
        }

        std::string sessionToDelete;
        std::string protocolVersion;
        {
            std::lock_guard<std::mutex> lk(sessionMutex);
            sessionToDelete = mcpSessionId;
            protocolVersion = negotiatedProtocolVersion;
        }
        if (sessionToDelete.empty()) {
            co_return exchange;
        }

        try {
            http::request<http::string_body> req{http::verb::delete_, requestPath(), 11};
            req.set(http::field::accept, "application/json");
            req.set(http::field::connection, "close");
            req.set("Mcp-Session-Id", sessionToDelete);
            if (!protocolVersion.empty()) {
                req.set("MCP-Protocol-Version", protocolVersion);
            }
            co_await coEnsureAuthHeader(req);

            auto ex = co_await net::this_coro::executor;
            tcp::resolver resolver(ex);
            auto results = co_await resolver.async_resolve(opts.host, opts.port, net::use_awaitable);

            if (opts.scheme == "https") {
                if (!caInitOk) {
                    setError("HTTPS: CA initialization failed (bad caFile/caPath)");
                    co_return exchange;
                }
                boost::beast::ssl_stream<boost::beast::tcp_stream> stream(ex, *sslCtx);
                if (!opts.serverName.empty()) {
                    if (!::SSL_set_tlsext_host_name(stream.native_handle(), opts.serverName.c_str())) {
                        setError("HTTPS: failed to set SNI hostname");
                    }
                    (void)::SSL_set1_host(stream.native_handle(), opts.serverName.c_str());
                }
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                co_await stream.next_layer().async_connect(results, net::use_awaitable);
                co_await stream.async_handshake(ssl::stream_base::client, net::use_awaitable);
                req.set(http::field::host, opts.serverName.empty() ? opts.host : opts.serverName);
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
                co_await http::async_write(stream, req, net::use_awaitable);
                boost::beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, net::use_awaitable);
                exchange = makeExchangeResult(res);
                boost::system::error_code ec;
                stream.shutdown(ec);
                co_return exchange;
            }

            boost::beast::tcp_stream stream(ex);
            stream.expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
            co_await stream.async_connect(results, net::use_awaitable);
            req.set(http::field::host, opts.host);
            stream.expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
            co_await http::async_write(stream, req, net::use_awaitable);
            boost::beast::flat_buffer buffer;
            http::response<http::string_body> res;
            co_await http::async_read(stream, buffer, res, net::use_awaitable);
            exchange = makeExchangeResult(res);
            boost::system::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            co_return exchange;
        } catch (const std::exception& e) {
            setError(std::string("HTTP coDeleteSession failed: ") + e.what());
        }
        co_return exchange;
    }
};

void HTTPTransport::Impl::handleIncomingJson(const std::string& json, const std::string& fallbackId) {
    if (!router) {
        if (!fallbackId.empty()) {
            deliverInternalError(fallbackId, "Router unavailable");
        }
        return;
    }

    RouterHandlers handlers{};
    handlers.requestHandler = [this](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        if (requestHandler) {
            return requestHandler(req);
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotAllowed, "No request handler registered", std::nullopt);
    };
    handlers.notificationHandler = notificationHandler;
    handlers.errorHandler = errorHandler;

    bool delivered = false;
    auto resolve = [this, &delivered, fallbackId](JSONRPCResponse&& response) mutable {
        delivered = deliverResponse(std::move(response), fallbackId);
    };

    auto routed = router->route(json, handlers, resolve);
    if (routed.has_value()) {
        net::co_spawn(ioc, coPostJson(notifyPath(), routed.value()),
            [this](std::exception_ptr eptr, HttpExchangeResult exchange) {
                if (eptr) {
                    try {
                        std::rethrow_exception(eptr);
                    } catch (const std::exception& e) {
                        setError(std::string("HTTP SSE response POST failed: ") + e.what());
                    }
                } else {
                    maybeCaptureSessionId(exchange);
                }
            });
        return;
    }

    if (!delivered && !fallbackId.empty()) {
        deliverInternalError(fallbackId, "Invalid/empty HTTP response");
    }
}

void HTTPTransport::Impl::maybeStartSseLoop() {
    if (!useStreamableHttp() || !opts.enableGetStream || !sseEnabled.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lk(sessionMutex);
        if (mcpSessionId.empty() || sseRunning.load()) {
            return;
        }
    }
    sseRunning.store(true);
    sseThread = std::thread([this]() { runSseLoop(); });
}

void HTTPTransport::Impl::runSseLoop() {
    auto parseEventStream = [this](std::istream& bodyStream, std::string& pendingData, unsigned int& retryMs) {
        std::string line;
        while (std::getline(bodyStream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                if (!pendingData.empty()) {
                    handleIncomingJson(pendingData);
                    pendingData.clear();
                }
                continue;
            }
            if (line.rfind("data:", 0) == 0) {
                std::string value = line.substr(5);
                if (!value.empty() && value.front() == ' ') {
                    value.erase(value.begin());
                }
                if (!pendingData.empty()) {
                    pendingData.push_back('\n');
                }
                pendingData += value;
            } else if (line.rfind("retry:", 0) == 0) {
                std::string value = line.substr(6);
                if (!value.empty() && value.front() == ' ') {
                    value.erase(value.begin());
                }
                try {
                    retryMs = static_cast<unsigned int>(std::stoul(value));
                } catch (...) {
                }
            }
        }
    };

    auto sleepForRetry = [this](unsigned int retryMs) {
        if (!sseEnabled.load() || !connected.load()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(retryMs));
    };

    auto runPlainLoop = [this, &parseEventStream, &sleepForRetry]() {
        unsigned int retryMs = opts.sseReconnectDelayMs;
        const unsigned int sseReadTimeoutMs = std::min(opts.readTimeoutMs, 1000u);
        while (connected.load() && sseEnabled.load()) {
            try {
                boost::asio::io_context iocLocal;
                tcp::resolver resolver{iocLocal};
                auto results = resolver.resolve(opts.host, opts.port);
                boost::beast::tcp_stream stream{iocLocal};
                stream.expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                stream.connect(results);

                http::request<http::string_body> req{http::verb::get, streamPath(), 11};
                req.set(http::field::host, opts.host);
                req.set(http::field::accept, "text/event-stream");
                {
                    std::lock_guard<std::mutex> lk(sessionMutex);
                    if (mcpSessionId.empty()) {
                        break;
                    }
                    req.set("Mcp-Session-Id", mcpSessionId);
                    if (!negotiatedProtocolVersion.empty()) {
                        req.set("MCP-Protocol-Version", negotiatedProtocolVersion);
                    }
                }
                auto authFuture = net::co_spawn(ioc, coEnsureAuthHeader(req), net::use_future);
                authFuture.get();
                http::write(stream, req);

                boost::asio::streambuf responseBuffer;
                stream.expires_after(std::chrono::milliseconds(sseReadTimeoutMs));
                boost::asio::read_until(stream, responseBuffer, "\r\n\r\n");
                std::istream headerStream(&responseBuffer);
                std::string statusLine;
                std::getline(headerStream, statusLine);
                if (statusLine.find("200") == std::string::npos) {
                    if (statusLine.find("405") != std::string::npos) {
                        sseEnabled.store(false);
                        break;
                    }
                    sleepForRetry(retryMs);
                    continue;
                }
                std::string headerLine;
                while (std::getline(headerStream, headerLine) && headerLine != "\r") {
                    if (!headerLine.empty() && headerLine.back() == '\r') {
                        headerLine.pop_back();
                    }
                    auto colon = headerLine.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    std::string name = headerLine.substr(0, colon);
                    std::string value = headerLine.substr(colon + 1);
                    while (!value.empty() && value.front() == ' ') {
                        value.erase(value.begin());
                    }
                    if (name == std::string("Mcp-Session-Id")) {
                        std::lock_guard<std::mutex> lk(sessionMutex);
                        mcpSessionId = value;
                    }
                }

                std::string pendingData;
                while (connected.load() && sseEnabled.load()) {
                    headerStream.clear();
                    parseEventStream(headerStream, pendingData, retryMs);
                    if (!connected.load() || !sseEnabled.load()) {
                        break;
                    }
                    boost::system::error_code ec;
                    stream.expires_after(std::chrono::milliseconds(sseReadTimeoutMs));
                    std::size_t bytes = boost::asio::read_until(stream, responseBuffer, '\n', ec);
                    if (ec) {
                        break;
                    }
                    (void)bytes;
                }
            } catch (const std::exception& e) {
                setError(std::string("HTTP SSE loop error: ") + e.what());
            }
            sleepForRetry(retryMs);
        }
    };

    auto runTlsLoop = [this, &parseEventStream, &sleepForRetry]() {
        unsigned int retryMs = opts.sseReconnectDelayMs;
        const unsigned int sseReadTimeoutMs = std::min(opts.readTimeoutMs, 1000u);
        while (connected.load() && sseEnabled.load()) {
            try {
                boost::asio::io_context iocLocal;
                tcp::resolver resolver{iocLocal};
                auto results = resolver.resolve(opts.host, opts.port);
                boost::beast::ssl_stream<boost::beast::tcp_stream> stream{iocLocal, *sslCtx};
                if (!opts.serverName.empty()) {
                    (void)::SSL_set_tlsext_host_name(stream.native_handle(), opts.serverName.c_str());
                    (void)::SSL_set1_host(stream.native_handle(), opts.serverName.c_str());
                }
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                stream.next_layer().connect(results);
                stream.handshake(ssl::stream_base::client);

                http::request<http::string_body> req{http::verb::get, streamPath(), 11};
                req.set(http::field::host, opts.serverName.empty() ? opts.host : opts.serverName);
                req.set(http::field::accept, "text/event-stream");
                {
                    std::lock_guard<std::mutex> lk(sessionMutex);
                    if (mcpSessionId.empty()) {
                        break;
                    }
                    req.set("Mcp-Session-Id", mcpSessionId);
                    if (!negotiatedProtocolVersion.empty()) {
                        req.set("MCP-Protocol-Version", negotiatedProtocolVersion);
                    }
                }
                auto authFuture = net::co_spawn(ioc, coEnsureAuthHeader(req), net::use_future);
                authFuture.get();
                http::write(stream, req);

                boost::asio::streambuf responseBuffer;
                stream.next_layer().expires_after(std::chrono::milliseconds(sseReadTimeoutMs));
                boost::asio::read_until(stream, responseBuffer, "\r\n\r\n");
                std::istream headerStream(&responseBuffer);
                std::string statusLine;
                std::getline(headerStream, statusLine);
                if (statusLine.find("200") == std::string::npos) {
                    if (statusLine.find("405") != std::string::npos) {
                        sseEnabled.store(false);
                        break;
                    }
                    sleepForRetry(retryMs);
                    continue;
                }
                std::string headerLine;
                while (std::getline(headerStream, headerLine) && headerLine != "\r") {
                    if (!headerLine.empty() && headerLine.back() == '\r') {
                        headerLine.pop_back();
                    }
                    auto colon = headerLine.find(':');
                    if (colon == std::string::npos) {
                        continue;
                    }
                    std::string name = headerLine.substr(0, colon);
                    std::string value = headerLine.substr(colon + 1);
                    while (!value.empty() && value.front() == ' ') {
                        value.erase(value.begin());
                    }
                    if (name == std::string("Mcp-Session-Id")) {
                        std::lock_guard<std::mutex> lk(sessionMutex);
                        mcpSessionId = value;
                    }
                }

                std::string pendingData;
                while (connected.load() && sseEnabled.load()) {
                    headerStream.clear();
                    parseEventStream(headerStream, pendingData, retryMs);
                    if (!connected.load() || !sseEnabled.load()) {
                        break;
                    }
                    boost::system::error_code ec;
                    stream.next_layer().expires_after(std::chrono::milliseconds(sseReadTimeoutMs));
                    std::size_t bytes = boost::asio::read_until(stream, responseBuffer, '\n', ec);
                    if (ec) {
                        break;
                    }
                    (void)bytes;
                }
                boost::system::error_code ec;
                stream.shutdown(ec);
            } catch (const std::exception& e) {
                setError(std::string("HTTPS SSE loop error: ") + e.what());
            }
            sleepForRetry(retryMs);
        }
    };

    if (opts.scheme == "https") {
        runTlsLoop();
    } else {
        runPlainLoop();
    }
    sseRunning.store(false);
}

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
            if (pImpl->errorHandler) {
                pImpl->errorHandler(e.what());
            }
            pr.set_value();
        }
    });
    return fut;
}

std::future<void> HTTPTransport::Close() {
    FUNC_SCOPE();
    std::promise<void> done; auto fut = done.get_future();
    try {
        pImpl->closing.store(true);
        if (pImpl->connected.load() && pImpl->useStreamableHttp() && pImpl->opts.enableDeleteOnClose) {
            try {
                auto deleteFuture = net::co_spawn(pImpl->ioc, pImpl->coDeleteSession(), net::use_future);
                (void)deleteFuture.get();
            } catch (const std::exception& e) {
                if (pImpl->errorHandler) {
                    pImpl->errorHandler(std::string("HTTP Close: delete session failed: ") + e.what());
                }
            }
        }
        pImpl->stopSseLoop();
        if (pImpl->errorHandler) {
            pImpl->errorHandler("HTTP Close: begin");
        }
        pImpl->connected.store(false);
        {
            std::lock_guard<std::mutex> lk(pImpl->sessionMutex);
            pImpl->mcpSessionId.clear();
        }
        if (pImpl->workGuard) {
            pImpl->workGuard->reset(); pImpl->workGuard.reset();
        }
        pImpl->ioc.stop();
        if (pImpl->errorHandler) {
            pImpl->errorHandler("HTTP Close: ioc.stop() called");
        }
        if (pImpl->ioThread.joinable()) {
            if (pImpl->errorHandler) {
                pImpl->errorHandler("HTTP Close: joining ioThread");
            }
            pImpl->ioThread.join();
            if (pImpl->errorHandler) {
                pImpl->errorHandler("HTTP Close: ioThread joined");
            }
        }
        
        // Fail any pending requests like InMemoryTransport
        {
            std::lock_guard<std::mutex> lk(pImpl->requestMutex);
            if (pImpl->errorHandler) {
                pImpl->errorHandler(std::string("HTTP Close: failing pending size=") + std::to_string(pImpl->pendingRequests.size()));
            }
            for (auto& kv : pImpl->pendingRequests) {
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = kv.first;
                resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport closed", std::nullopt);
                kv.second.set_value(std::move(resp));
            }
            pImpl->pendingRequests.clear();
        }
    } catch (...) {
        if (pImpl->errorHandler) {
            pImpl->errorHandler("HTTP Close: exception");
        }
    }
    done.set_value();
    return fut;
}

bool HTTPTransport::IsConnected() const {
    FUNC_SCOPE(); return pImpl->connected.load();
}
std::string HTTPTransport::GetSessionId() const {
    FUNC_SCOPE();
    std::lock_guard<std::mutex> lk(pImpl->sessionMutex);
    return pImpl->mcpSessionId.empty() ? pImpl->sessionId : pImpl->mcpSessionId;
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
            if constexpr (std::is_same_v<T, std::string>) { 
                if (!idVal.empty()) {
                    callerSetId = true;
                }
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                callerSetId = true;
            }
        }, request->id);
        if (!callerSetId) {
            const std::string newId = pImpl->generateRequestId();
            request->id = newId;
        }
    }
    const bool startsStreamableSession = pImpl->useStreamableHttp() && request && request->method == Methods::Initialize;
    std::string payload = request ? request->Serialize() : std::string();
    
    // Compute id string for map key
    std::string idStr;
    if (request) {
        std::visit([&](const auto& id){
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, std::string>) {
                idStr = id;
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                idStr = std::to_string(id);
            }
        }, request->id);
    }
    {
        std::lock_guard<std::mutex> lk(pImpl->requestMutex);
        pImpl->pendingRequests[idStr] = std::move(promise);
    }

    net::co_spawn(pImpl->ioc, pImpl->coPostJson(pImpl->requestPath(), std::move(payload)),
        [this, idStr, startsStreamableSession](std::exception_ptr eptr, HTTPTransport::Impl::HttpExchangeResult exchange) mutable {
            if (eptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& e) {
                    if (pImpl->errorHandler) {
                        pImpl->errorHandler(e.what());
                    }
                }
            }

            pImpl->maybeCaptureSessionId(exchange);
            if (startsStreamableSession && !exchange.sessionId.empty()) {
                pImpl->maybeStartSseLoop();
            }

            if (!exchange.body.empty()) {
                pImpl->handleIncomingJson(exchange.body, idStr);
                return;
            }

            pImpl->deliverInternalError(idStr, "Invalid/empty HTTP response");
        });

    return fut;
}

std::future<void> HTTPTransport::SendNotification(
    std::unique_ptr<JSONRPCNotification> notification) {
    FUNC_SCOPE();
    std::promise<void> done; auto fut = done.get_future();
    if (!pImpl->connected.load()) { done.set_value(); return fut; }
    std::string payload = notification ? notification->Serialize() : std::string();

    net::co_spawn(pImpl->ioc, pImpl->coPostJson(pImpl->notifyPath(), std::move(payload)),
        [this, pr = std::move(done)](std::exception_ptr eptr, HTTPTransport::Impl::HttpExchangeResult exchange) mutable {
            if (eptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& e) {
                    if (pImpl->errorHandler) {
                        pImpl->errorHandler(e.what());
                    }
                }
            }
            pImpl->maybeCaptureSessionId(exchange);
            pr.set_value();
        });

    return fut;
}

void HTTPTransport::SetNotificationHandler(NotificationHandler handler) {
    FUNC_SCOPE();
    pImpl->notificationHandler = std::move(handler);
}

void HTTPTransport::SetRequestHandler(RequestHandler handler) {
    FUNC_SCOPE();
    pImpl->requestHandler = std::move(handler);
}

void HTTPTransport::SetErrorHandler(ErrorHandler handler) {
    FUNC_SCOPE();
    pImpl->errorHandler = std::move(handler);
    if (pImpl->errorHandler) {
        mcp::auth::IAuth* a = pImpl->authStrong ? pImpl->authStrong.get() : pImpl->authWeak;
        if (a) {
            a->setErrorHandler(pImpl->errorHandler);
        }
    }
}

void HTTPTransport::SetAuth(mcp::auth::IAuth& auth) {
    FUNC_SCOPE();
    pImpl->authStrong.reset();
    pImpl->authWeak = &auth;
    if (pImpl->errorHandler) {
        auth.setErrorHandler(pImpl->errorHandler);
    }
}

void HTTPTransport::SetAuth(std::shared_ptr<mcp::auth::IAuth> auth) {
    FUNC_SCOPE();
    pImpl->authWeak = nullptr;
    pImpl->authStrong = std::move(auth);
    if (pImpl->errorHandler && pImpl->authStrong) {
        pImpl->authStrong->setErrorHandler(pImpl->errorHandler);
    }
}

bool HTTPTransport::QueryLastHttpResponse(HttpResponseInfo& out) const {
    FUNC_SCOPE();
    // Rationale: return true as soon as ANY meaningful part of the last HTTP response snapshot
    // is available (OR semantics), rather than requiring ALL fields (AND semantics).
    // - Real responses may legitimately omit fields (e.g., 200 OK has no WWW-Authenticate).
    // - We record status/headers opportunistically; OR semantics avoids false negatives and remains
    //   resilient if future changes only capture a subset (e.g., headers or a challenge line).
    {
        std::lock_guard<std::mutex> lk(pImpl->lastRespMutex);
        out = pImpl->lastResp;
    }
    if (out.status != 0) {
        return true;
    }
    if (!out.headers.empty()) {
        return true;
    }
    if (!out.wwwAuthenticate.empty()) {
        return true;
    }
    return false;
}

//==========================================================================================================
// HTTPTransportFactory::CreateTransport
// Purpose: Parse semicolon-delimited key=value config into Options and create transport.
//==========================================================================================================
std::unique_ptr<mcp::ITransport> mcp::HTTPTransportFactory::CreateTransport(const std::string& config) {
    HTTPTransport::Options opts;
    auto trim = [](std::string s) -> std::string {
        std::size_t b = 0, e = s.size();
        while (b < e && (s[b] == ' ' || s[b] == '\t')) {
            ++b;
        }
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) {
            --e;
        }
        return s.substr(b, e - b);
    };

    if (!config.empty()) {
        std::size_t start = 0;
        while (start < config.size()) {
            std::size_t sep = config.find(';', start);
            if (sep == std::string::npos) { sep = config.size(); }
            std::string kv = trim(config.substr(start, sep - start));
            if (!kv.empty()) {
                std::size_t eq = kv.find('=');
                if (eq != std::string::npos) {
                    std::string key = trim(kv.substr(0, eq));
                    std::string val = trim(kv.substr(eq + 1));
                    if (key == "scheme") {
                        opts.scheme = val;
                    }
                    else if (key == "host") {
                        opts.host = val;
                    }
                    else if (key == "port") {
                        opts.port = val;
                    }
                    else if (key == "endpointPath") {
                        opts.endpointPath = val;
                    }
                    else if (key == "streamPath") {
                        opts.streamPath = val;
                    }
                    else if (key == "rpcPath") {
                        opts.rpcPath = val;
                    }
                    else if (key == "notifyPath") {
                        opts.notifyPath = val;
                    }
                    else if (key == "serverName") {
                        opts.serverName = val;
                    }
                    else if (key == "caFile") {
                        opts.caFile = val;
                    }
                    else if (key == "caPath") {
                        opts.caPath = val;
                    }
                    else if (key == "connectTimeoutMs") {
                        try {
                            opts.connectTimeoutMs = static_cast<unsigned int>(std::stoul(val));
                        } catch (...) {}
                    }
                    else if (key == "readTimeoutMs") {
                        try {
                            opts.readTimeoutMs = static_cast<unsigned int>(std::stoul(val));
                        } catch (...) {}
                    }
                    else if (key == "sseReconnectDelayMs") {
                        try {
                            opts.sseReconnectDelayMs = static_cast<unsigned int>(std::stoul(val));
                        } catch (...) {}
                    }
                    else if (key == "enableGetStream") {
                        opts.enableGetStream = !(val == "0" || val == "false" || val == "False");
                    }
                    else if (key == "enableDeleteOnClose") {
                        opts.enableDeleteOnClose = !(val == "0" || val == "false" || val == "False");
                    }
                    else if (key == "auth") {
                        opts.auth = val;
                    }
                    else if (key == "token" || key == "bearerToken") {
                        opts.bearerToken = val;
                    }
                    else if (key == "oauthUrl" || key == "oauthTokenUrl") {
                        opts.oauthTokenUrl = val;
                    }
                    else if (key == "clientId") {
                        opts.clientId = val;
                    }
                    else if (key == "clientSecret") {
                        opts.clientSecret = val;
                    }
                    else if (key == "scope") {
                        opts.scope = val;
                    }
                    else if (key == "tokenRefreshSkewSeconds" || key == "tokenSkew") {
                        try {
                            opts.tokenRefreshSkewSeconds = static_cast<unsigned int>(std::stoul(val));
                        } catch (...) {}
                    }
                }
            }
            start = sep + 1;
        }
    }
    return std::make_unique<HTTPTransport>(opts);
}

} // namespace mcp
