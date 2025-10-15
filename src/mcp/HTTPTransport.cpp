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

    // OAuth token cache (for auth == oauth2)
    std::mutex tokenMutex;
    std::string cachedAccessToken;
    std::chrono::steady_clock::time_point tokenExpiry{};

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

    // ------------------------------------------------------------------------------------------------------
    // URL parsing helpers (very small, adequate for https://host[:port]/path)
    // ------------------------------------------------------------------------------------------------------
    struct UrlParts {
        std::string scheme;
        std::string host;
        std::string port;
        std::string path;
        std::string serverName;
    };

    static UrlParts parseUrl(const std::string& url) {
        UrlParts parts;
        std::size_t pos = 0;

        std::size_t schemeEnd = url.find("://");
        if (schemeEnd != std::string::npos) {
            parts.scheme = url.substr(0, schemeEnd);
            pos = schemeEnd + 3;
        } else {
            parts.scheme = std::string("http");
            pos = 0;
        }

        std::size_t slash = url.find('/', pos);
        std::string hostPort;
        if (slash == std::string::npos) {
            hostPort = url.substr(pos);
            parts.path = std::string("/");
        } else {
            hostPort = url.substr(pos, slash - pos);
            parts.path = url.substr(slash);
        }

        std::size_t colon = hostPort.find(':');
        if (colon == std::string::npos) {
            parts.host = hostPort;
            if (parts.scheme == std::string("https")) {
                parts.port = std::string("443");
            } else {
                parts.port = std::string("80");
            }
        } else {
            parts.host = hostPort.substr(0, colon);
            parts.port = hostPort.substr(colon + 1);
        }

        parts.serverName = parts.host;
        return parts;
    }

    static std::string urlEncodeForm(const std::string& s) {
        std::ostringstream oss;
        for (std::size_t i = 0; i < s.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
                oss << static_cast<char>(c);
            } else if (c == ' ') {
                oss << '+';
            } else {
                oss << '%';
                const char* hex = "0123456789ABCDEF";
                oss << hex[(c >> 4) & 0xFu] << hex[c & 0xFu];
            }
        }
        return oss.str();
    }

    static bool parseJsonStringField(const std::string& json, const std::string& key, std::string& out) {
        out.clear();
        std::string needle = std::string("\"") + key + std::string("\"");
        std::size_t k = json.find(needle);
        if (k == std::string::npos) {
            return false;
        }
        std::size_t colon = json.find(':', k + needle.size());
        if (colon == std::string::npos) {
            return false;
        }
        std::size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos) {
            return false;
        }
        std::size_t q2 = q1 + 1;
        while (q2 < json.size()) {
            if (json[q2] == '"' && json[q2 - 1] != '\\') {
                break;
            }
            q2 += 1;
        }
        if (q2 >= json.size()) {
            return false;
        }
        out = json.substr(q1 + 1, q2 - q1 - 1);
        return true;
    }

    static bool parseJsonIntField(const std::string& json, const std::string& key, int& out) {
        std::string needle = std::string("\"") + key + std::string("\"");
        std::size_t k = json.find(needle);
        if (k == std::string::npos) {
            return false;
        }
        std::size_t colon = json.find(':', k + needle.size());
        if (colon == std::string::npos) {
            return false;
        }
        std::size_t i = colon + 1;
        while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
            i += 1;
        }
        std::size_t j = i;
        while (j < json.size() && ((json[j] >= '0' && json[j] <= '9') || json[j] == '-')) {
            j += 1;
        }
        try {
            out = std::stoi(json.substr(i, j - i));
            return true;
        } catch (...) {
            return false;
        }
    }

    // Coroutine: POST application/x-www-form-urlencoded to arbitrary URL and return response body
    net::awaitable<std::string> coPostFormUrlencoded(const std::string& url, const std::string& body) {
        try {
            UrlParts u = parseUrl(url);
            tcp::resolver resolver(co_await net::this_coro::executor);
            auto results = co_await resolver.async_resolve(u.host, u.port, net::use_awaitable);
            if (errorHandler) { errorHandler(std::string("HTTP DEBUG: token resolved ") + u.host + std::string(":") + u.port + std::string(" path=") + u.path); }

            if (u.scheme == std::string("https")) {
                ssl::context* ctxPtr = nullptr;
                std::unique_ptr<ssl::context> localCtx;
                if (sslCtx) {
                    ctxPtr = sslCtx.get();
                } else {
                    localCtx = std::make_unique<ssl::context>(ssl::context::tls_client);
                    ::SSL_CTX_set_min_proto_version(localCtx->native_handle(), TLS1_3_VERSION);
                    ::SSL_CTX_set_max_proto_version(localCtx->native_handle(), TLS1_3_VERSION);
                    try {
                        localCtx->set_default_verify_paths();
                    } catch (...) {
                        // best-effort
                    }
                    localCtx->set_verify_mode(ssl::verify_peer);
                    ctxPtr = localCtx.get();
                }

                boost::beast::ssl_stream<boost::beast::tcp_stream> stream(co_await net::this_coro::executor, *ctxPtr);
                if (!u.serverName.empty()) {
                    if (!::SSL_set_tlsext_host_name(stream.native_handle(), u.serverName.c_str())) {
                        setError("HTTPS: failed to set SNI hostname");
                    }
                    (void)::SSL_set1_host(stream.native_handle(), u.serverName.c_str());
                }
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                co_await stream.next_layer().async_connect(results, net::use_awaitable);
                if (errorHandler) { errorHandler("HTTP DEBUG: token https connected"); }
                co_await stream.async_handshake(ssl::stream_base::client, net::use_awaitable);
                if (errorHandler) { errorHandler("HTTP DEBUG: token https handshake complete"); }

                http::request<http::string_body> req{http::verb::post, u.path, 11};
                req.set(http::field::host, u.serverName);
                req.set(http::field::content_type, "application/x-www-form-urlencoded");
                req.set(http::field::accept, "application/json");
                req.set(http::field::connection, "close");
                req.body() = body;
                req.prepare_payload();

                stream.next_layer().expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
                co_await http::async_write(stream, req, net::use_awaitable);
                if (errorHandler) { errorHandler("HTTP DEBUG: token https wrote form"); }
                boost::beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, net::use_awaitable);
                if (errorHandler) { errorHandler(std::string("HTTP DEBUG: token https read response bytes=") + std::to_string(res.body().size())); }
                boost::system::error_code ec; 
                stream.shutdown(ec);
                co_return res.body();
            } else {
                boost::beast::tcp_stream stream(co_await net::this_coro::executor);
                stream.expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                co_await stream.async_connect(results, net::use_awaitable);
                if (errorHandler) { errorHandler("HTTP DEBUG: token http connected"); }

                http::request<http::string_body> req{http::verb::post, u.path, 11};
                req.set(http::field::host, u.host);
                req.set(http::field::content_type, "application/x-www-form-urlencoded");
                req.set(http::field::accept, "application/json");
                req.set(http::field::connection, "close");
                req.body() = body;
                req.prepare_payload();

                stream.expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
                co_await http::async_write(stream, req, net::use_awaitable);
                if (errorHandler) { errorHandler("HTTP DEBUG: token http wrote form"); }
                boost::beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, net::use_awaitable);
                if (errorHandler) { errorHandler(std::string("HTTP DEBUG: token http read response bytes=") + std::to_string(res.body().size())); }
                boost::system::error_code ec;
                stream.socket().shutdown(tcp::socket::shutdown_both, ec);
                co_return res.body();
            }
        } catch (const std::exception& e) {
            setError(std::string("HTTP coPostFormUrlencoded failed: ") + e.what());
        }
        co_return std::string();
    }

    // Coroutine: ensure Authorization header is set per opts.auth; may fetch OAuth token
    net::awaitable<void> coEnsureAuthHeader(http::request<http::string_body>& req) {
        if (opts.auth == std::string("bearer")) {
            if (!opts.bearerToken.empty()) {
                req.set(http::field::authorization, std::string("Bearer ") + opts.bearerToken);
            }
            co_return;
        }

        if (opts.auth == std::string("oauth2")) {
            bool haveValid = false;
            std::string token;
            {
                std::lock_guard<std::mutex> lk(tokenMutex);
                auto now = std::chrono::steady_clock::now();
                if (!cachedAccessToken.empty()) {
                    if (now + std::chrono::seconds(opts.tokenRefreshSkewSeconds) < tokenExpiry) {
                        haveValid = true;
                        token = cachedAccessToken;
                    }
                }
            }

            if (!haveValid) {
                // Fetch new token
                std::ostringstream form;
                form << "grant_type=client_credentials";
                if (!opts.clientId.empty()) {
                    form << "&client_id=" << urlEncodeForm(opts.clientId);
                }
                if (!opts.clientSecret.empty()) {
                    form << "&client_secret=" << urlEncodeForm(opts.clientSecret);
                }
                if (!opts.scope.empty()) {
                    form << "&scope=" << urlEncodeForm(opts.scope);
                }
                std::string body = form.str();
                std::string resp = co_await coPostFormUrlencoded(opts.oauthTokenUrl, body);
                if (!resp.empty()) {
                    std::string at;
                    int expiresIn = 0;
                    bool ok1 = parseJsonStringField(resp, std::string("access_token"), at);
                    bool ok2 = parseJsonIntField(resp, std::string("expires_in"), expiresIn);
                    if (ok1) {
                        std::lock_guard<std::mutex> lk(tokenMutex);
                        cachedAccessToken = at;
                        if (ok2 && expiresIn > 0) {
                            tokenExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(static_cast<unsigned int>(expiresIn));
                        } else {
                            tokenExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(3600);
                        }
                        token = cachedAccessToken;
                        haveValid = true;
                        if (errorHandler) { errorHandler(std::string("OAuth2: token obtained; expiresIn=") + std::to_string(ok2 ? expiresIn : 3600)); }
                    } else {
                        setError("OAuth2: token endpoint response missing access_token");
                    }
                } else {
                    setError("OAuth2: empty response from token endpoint");
                }
            }

            if (haveValid) {
                req.set(http::field::authorization, std::string("Bearer ") + token);
            }
        }
        co_return;
    }

    // Coroutine: POST JSON and return response body
    net::awaitable<std::string> coPostJson(const std::string path, const std::string body) {
        try {
            // Build request first and ensure Authorization header (may perform OAuth token fetch)
            http::request<http::string_body> req{http::verb::post, path, 11};
            req.set(http::field::content_type, "application/json");
            req.set(http::field::accept, "application/json");
            req.set(http::field::connection, "close");
            req.body() = body;
            req.prepare_payload();
            // Authorization header (bearer/oauth2) before opening RPC connection to avoid server read deadlock
            co_await coEnsureAuthHeader(req);

            tcp::resolver resolver(co_await net::this_coro::executor);
            auto results = co_await resolver.async_resolve(opts.host, opts.port, net::use_awaitable);
            if (errorHandler) { errorHandler(std::string("HTTP DEBUG: resolved ") + opts.host + std::string(":") + opts.port + std::string(" path=") + path); }

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

                req.set(http::field::host, opts.serverName.empty() ? opts.host : opts.serverName);
                // Set timeouts
                stream.next_layer().expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
                co_await http::async_write(stream, req, net::use_awaitable);
                if (errorHandler) { errorHandler("HTTP DEBUG: https wrote request"); }
                boost::beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, net::use_awaitable);
                if (errorHandler) { errorHandler(std::string("HTTP DEBUG: https read response bytes=") + std::to_string(res.body().size())); }
                boost::system::error_code ec; stream.shutdown(ec);
                co_return res.body();
            } else {
                boost::beast::tcp_stream stream(co_await net::this_coro::executor);
                stream.expires_after(std::chrono::milliseconds(opts.connectTimeoutMs));
                co_await stream.async_connect(results, net::use_awaitable);
                if (errorHandler) { errorHandler("HTTP DEBUG: http connected"); }

                req.set(http::field::host, opts.host);
                stream.expires_after(std::chrono::milliseconds(opts.readTimeoutMs));
                co_await http::async_write(stream, req, net::use_awaitable);
                if (errorHandler) { errorHandler("HTTP DEBUG: http wrote request"); }
                boost::beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, net::use_awaitable);
                if (errorHandler) { errorHandler(std::string("HTTP DEBUG: http read response bytes=") + std::to_string(res.body().size())); }
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
        if (pImpl->errorHandler) {
            pImpl->errorHandler("HTTP Close: begin");
        }
        pImpl->connected.store(false);
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
            if constexpr (std::is_same_v<T, std::string>) { 
                if (!idVal.empty()) {
                    callerSetId = true;
                }
            }
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

    net::co_spawn(pImpl->ioc, pImpl->coPostJson(pImpl->opts.rpcPath, payload),
        [this, idStr](std::exception_ptr eptr, std::string body) mutable {
            if (pImpl->errorHandler) {
                pImpl->errorHandler(std::string("HTTPTransport: coPostJson done; body.size=") + std::to_string(body.size()) + std::string("; key=") + idStr);
            }
            if (eptr) {
                try {
                    std::rethrow_exception(eptr);
                } catch (const std::exception& e) {
                    if (pImpl->errorHandler) {
                        pImpl->errorHandler(e.what());
                    }
                }
            }
            std::unique_ptr<JSONRPCResponse> out;
            if (!body.empty()) {
                auto resp = std::make_unique<JSONRPCResponse>();
                if (resp->Deserialize(body)) {
                    if (pImpl->errorHandler) {
                        std::string respIdS;
                        std::visit([&](const auto& id){
                            using T = std::decay_t<decltype(id)>;
                            if constexpr (std::is_same_v<T, std::string>) {
                                respIdS = id;
                            }
                            else if constexpr (std::is_same_v<T, int64_t>) {
                                respIdS = std::to_string(id);
                            }
                            else {
                                respIdS = std::string("null");
                            }
                        }, resp->id);
                        pImpl->errorHandler(std::string("HTTPTransport: parsed response id=") + respIdS);
                    }
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
            
            // Deliver to pending promise: move promise out under lock, fulfill outside lock
            std::promise<std::unique_ptr<JSONRPCResponse>> deliverPromise;
            bool havePromise = false;
            {
                std::lock_guard<std::mutex> lk(pImpl->requestMutex);
                auto it = pImpl->pendingRequests.find(idStr);
                if (pImpl->errorHandler) {
                    pImpl->errorHandler(std::string("HTTPTransport: deliver lookup key=") + idStr + (it != pImpl->pendingRequests.end() ? std::string(" hit") : std::string(" miss")) + std::string("; pending=") + std::to_string(pImpl->pendingRequests.size()));
                }
                if (it != pImpl->pendingRequests.end()) {
                    deliverPromise = std::move(it->second);
                    pImpl->pendingRequests.erase(it);
                    havePromise = true;
                }
            }
            if (havePromise) {
                if (pImpl->errorHandler) {
                    pImpl->errorHandler("HTTPTransport: set_value start");
                }
                deliverPromise.set_value(std::move(out));
                if (pImpl->errorHandler) {
                    pImpl->errorHandler("HTTPTransport: set_value done");
                }
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
