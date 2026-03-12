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
#include <future>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>

#include "logging/Logger.h"
#include "mcp/HTTPServer.hpp"
#include "mcp/JSONRPCTypes.h"
#include "mcp/Protocol.h"
#include "mcp/auth/ServerAuth.hpp"
#include "mcp/JsonRpcMessageRouter.h"

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

    struct SessionState {
        std::string id;
        std::string protocolVersion{PROTOCOL_VERSION};
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::pair<std::string, std::string>> outboundMessages;
        std::unordered_map<std::string, std::promise<std::unique_ptr<JSONRPCResponse>>> pendingRequests;
        bool closed{false};
        uint64_t nextEventId{1u};
    };

    std::mutex sessionsMutex;
    std::unordered_map<std::string, std::shared_ptr<SessionState>> sessions;
    std::string activeSessionId;
    std::atomic<unsigned int> requestCounter{0u};
    std::mutex sseThreadsMutex;
    std::vector<std::thread> sseThreads;

    // Optional server-side Bearer authentication
    mcp::auth::ITokenVerifier* tokenVerifier{nullptr};
    mcp::auth::RequireBearerTokenOptions bearerOptions{};
    bool bearerAuthEnabled{false};

    std::promise<void> listenReadyPromise;
    std::atomic<bool> listenReadySignaled{false};

    std::unique_ptr<IJsonRpcMessageRouter> router;

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
        router = MakeDefaultJsonRpcMessageRouter();
    }

    ~Impl() {
        if (ioThread.joinable()) {
            ioc.stop();
            ioThread.join();
        }
        std::lock_guard<std::mutex> lk(sseThreadsMutex);
        for (auto& thread : sseThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void setError(const std::string& msg) {
        if (errorHandler) { errorHandler(msg); }
    }

    void signalReadyOrErrorOnce() {
        bool expected = false;
        if (listenReadySignaled.compare_exchange_strong(expected, true)) {
            try {
                listenReadyPromise.set_value();
            } catch (...) {}
        }
    }

    bool useStreamableHttp() const {
        return !opts.endpointPath.empty();
    }

    std::string endpointPath() const {
        return opts.endpointPath;
    }

    std::string streamPath() const {
        if (!opts.streamPath.empty()) {
            return opts.streamPath;
        }
        return opts.endpointPath;
    }

    std::string generateSessionId() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        return std::string("mcp-http-") + std::to_string(dis(gen));
    }

    std::string generateRequestId() {
        return std::string("http-server-req-") + std::to_string(++requestCounter);
    }

    std::shared_ptr<SessionState> findSession(const std::string& id) {
        std::lock_guard<std::mutex> lk(sessionsMutex);
        auto it = sessions.find(id);
        return it == sessions.end() ? nullptr : it->second;
    }

    std::shared_ptr<SessionState> createSession(const std::string& protocolVersion) {
        auto session = std::make_shared<SessionState>();
        session->id = generateSessionId();
        session->protocolVersion = protocolVersion.empty() ? std::string(PROTOCOL_VERSION) : protocolVersion;
        std::lock_guard<std::mutex> lk(sessionsMutex);
        sessions[session->id] = session;
        activeSessionId = session->id;
        return session;
    }

    std::shared_ptr<SessionState> activeSession() {
        std::lock_guard<std::mutex> lk(sessionsMutex);
        auto it = sessions.find(activeSessionId);
        return it == sessions.end() ? nullptr : it->second;
    }

    void removeSession(const std::string& id) {
        std::shared_ptr<SessionState> removed;
        std::vector<std::pair<std::string, std::promise<std::unique_ptr<JSONRPCResponse>>>> pending;
        {
            std::lock_guard<std::mutex> lk(sessionsMutex);
            auto it = sessions.find(id);
            if (it == sessions.end()) {
                return;
            }
            removed = it->second;
            sessions.erase(it);
            if (activeSessionId == id) {
                activeSessionId.clear();
            }
        }
        if (removed) {
            {
                std::lock_guard<std::mutex> lk(removed->mutex);
                removed->closed = true;
                for (auto& entry : removed->pendingRequests) {
                    pending.emplace_back(entry.first, std::move(entry.second));
                }
                removed->pendingRequests.clear();
                removed->cv.notify_all();
            }
            for (auto& entry : pending) {
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = entry.first;
                resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "HTTP session closed", std::nullopt);
                entry.second.set_value(std::move(resp));
            }
        }
    }

    void queueOutbound(const std::shared_ptr<SessionState>& session, const std::string& payload) {
        if (!session) {
            return;
        }
        std::lock_guard<std::mutex> lk(session->mutex);
        session->outboundMessages.emplace_back(std::to_string(session->nextEventId++), payload);
        session->cv.notify_all();
    }

    static std::string toLowerAscii(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    static std::string trimCopy(std::string value) {
        auto notSpace = [](unsigned char ch) { return std::isspace(ch) == 0; };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }

    bool shouldValidateDnsRebinding() const {
        std::string address = trimCopy(toLowerAscii(opts.address));
        if (address == "localhost") {
            return true;
        }
        boost::system::error_code ec;
        const auto parsed = net::ip::make_address(address, ec);
        return !ec && parsed.is_loopback();
    }

    static std::string extractAuthorityHost(const std::string& authorityValue) {
        std::string authority = trimCopy(authorityValue);
        if (authority.empty()) {
            return {};
        }
        if (authority.front() == '[') {
            const size_t closing = authority.find(']');
            if (closing == std::string::npos) {
                return {};
            }
            return authority.substr(1, closing - 1);
        }

        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos && authority.find(':') == colon) {
            return authority.substr(0, colon);
        }
        return authority;
    }

    static bool isLocalAuthorityHost(const std::string& hostValue) {
        const std::string host = toLowerAscii(trimCopy(hostValue));
        if (host.empty()) {
            return false;
        }
        if (host == "localhost") {
            return true;
        }
        boost::system::error_code ec;
        const auto parsed = net::ip::make_address(host, ec);
        return !ec && parsed.is_loopback();
    }

    static std::optional<std::string> extractOriginHost(const std::string& originHeader) {
        std::string origin = trimCopy(originHeader);
        if (origin.empty()) {
            return std::string();
        }
        const size_t schemePos = origin.find("://");
        if (schemePos == std::string::npos) {
            return std::nullopt;
        }
        origin = origin.substr(schemePos + 3);
        const size_t slash = origin.find('/');
        const std::string authority = slash == std::string::npos ? origin : origin.substr(0, slash);
        return extractAuthorityHost(authority);
    }

    bool validateLocalHeaders(const http::request<http::string_body>& req,
                              http::response<http::string_body>& res) const {
        if (!shouldValidateDnsRebinding()) {
            return true;
        }

        const std::string host = extractAuthorityHost(std::string(req[http::field::host]));
        if (!isLocalAuthorityHost(host)) {
            res.result(http::status::forbidden);
            res.body() = std::string("{\"error\":\"Forbidden Host header\"}");
            res.prepare_payload();
            return false;
        }

        const std::string originHeader = std::string(req["Origin"]);
        if (originHeader.empty()) {
            return true;
        }

        const auto originHost = extractOriginHost(originHeader);
        if (!originHost.has_value() || !isLocalAuthorityHost(originHost.value())) {
            res.result(http::status::forbidden);
            res.body() = std::string("{\"error\":\"Forbidden Origin header\"}");
            res.prepare_payload();
            return false;
        }
        return true;
    }

    bool validateProtocolHeader(const http::request<http::string_body>& req,
                                const std::shared_ptr<SessionState>& session,
                                http::response<http::string_body>& res) {
        const std::string versionHeader = std::string(req["MCP-Protocol-Version"]);
        if (versionHeader.empty()) {
            return true;
        }
        const std::string expected = session ? session->protocolVersion : std::string(PROTOCOL_VERSION);
        if (versionHeader != expected) {
            res.result(http::status::bad_request);
            res.body() = std::string("{\"error\":\"Unsupported protocol version\"}");
            res.prepare_payload();
            return false;
        }
        return true;
    }

    bool resolvePendingResponse(const std::shared_ptr<SessionState>& session, JSONRPCResponse&& response) {
        if (!session) {
            return false;
        }
        std::string respKey;
        std::visit([&](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, std::string>) {
                respKey = id;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                respKey = std::to_string(id);
            }
        }, response.id);
        if (respKey.empty()) {
            return false;
        }
        std::promise<std::unique_ptr<JSONRPCResponse>> deliverPromise;
        bool havePromise = false;
        {
            std::lock_guard<std::mutex> lk(session->mutex);
            auto it = session->pendingRequests.find(respKey);
            if (it != session->pendingRequests.end()) {
                deliverPromise = std::move(it->second);
                session->pendingRequests.erase(it);
                havePromise = true;
            }
        }
        if (havePromise) {
            deliverPromise.set_value(std::make_unique<JSONRPCResponse>(std::move(response)));
            return true;
        }
        return false;
    }

    net::awaitable<void> session_plain(tcp::socket socket) {
        try {
            boost::beast::tcp_stream stream(std::move(socket));
            boost::beast::flat_buffer buffer;
            for (;;) {
                http::request<http::string_body> req;
                co_await http::async_read(stream, buffer, req, net::use_awaitable);
                if (useStreamableHttp() &&
                    req.method() == http::verb::get &&
                    std::string(req.target()) == streamPath()) {
                    const std::string sessionHeader = std::string(req["Mcp-Session-Id"]);
                    auto session = findSession(sessionHeader);
                    http::response<http::string_body> res{http::status::bad_request, req.version()};
                    res.set(http::field::content_type, "application/json");
                    res.keep_alive(false);
                    if (!validateLocalHeaders(req, res)) {
                        co_await http::async_write(stream, res, net::use_awaitable);
                        break;
                    }
                    if (!session) {
                        res.result(http::status::bad_request);
                        res.body() = std::string("{\"error\":\"Missing or invalid session\"}");
                        res.prepare_payload();
                        co_await http::async_write(stream, res, net::use_awaitable);
                        break;
                    }
                    if (!validateProtocolHeader(req, session, res)) {
                        co_await http::async_write(stream, res, net::use_awaitable);
                        break;
                    }

                    tcp::socket sseSocket(std::move(stream.socket()));
                    std::lock_guard<std::mutex> lk(sseThreadsMutex);
                    sseThreads.emplace_back([this, s = std::move(sseSocket), session]() mutable {
                        try {
                            const std::string header =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/event-stream\r\n"
                                "Cache-Control: no-cache\r\n"
                                "Connection: close\r\n"
                                "Mcp-Session-Id: " + session->id + "\r\n\r\n";
                            boost::asio::write(s, boost::asio::buffer(header));
                            while (running.load()) {
                                std::pair<std::string, std::string> next;
                                {
                                    std::unique_lock<std::mutex> sessionLock(session->mutex);
                                    session->cv.wait_for(sessionLock, std::chrono::milliseconds(500), [&]() {
                                        return session->closed || !session->outboundMessages.empty() || !running.load();
                                    });
                                    if (session->closed || !running.load()) {
                                        break;
                                    }
                                    if (session->outboundMessages.empty()) {
                                        continue;
                                    }
                                    next = std::move(session->outboundMessages.front());
                                    session->outboundMessages.pop_front();
                                }
                                const std::string event = std::string("id: ") + next.first + std::string("\n") +
                                                          std::string("data: ") + next.second + std::string("\n\n");
                                boost::asio::write(s, boost::asio::buffer(event));
                            }
                        } catch (const std::exception& e) {
                            setError(std::string("HTTPServer SSE plain session error: ") + e.what());
                        }
                        boost::system::error_code ec;
                        s.shutdown(tcp::socket::shutdown_both, ec);
                        s.close(ec);
                    });
                    co_return;
                }
                auto res = co_await coMakeResponse(std::move(req));
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
                if (useStreamableHttp() &&
                    req.method() == http::verb::get &&
                    std::string(req.target()) == streamPath()) {
                    const std::string sessionHeader = std::string(req["Mcp-Session-Id"]);
                    auto session = findSession(sessionHeader);
                    http::response<http::string_body> res{http::status::bad_request, req.version()};
                    res.set(http::field::content_type, "application/json");
                    res.keep_alive(false);
                    if (!validateLocalHeaders(req, res)) {
                        co_await http::async_write(tls, res, net::use_awaitable);
                        break;
                    }
                    if (!session) {
                        res.result(http::status::bad_request);
                        res.body() = std::string("{\"error\":\"Missing or invalid session\"}");
                        res.prepare_payload();
                        co_await http::async_write(tls, res, net::use_awaitable);
                        break;
                    }
                    if (!validateProtocolHeader(req, session, res)) {
                        co_await http::async_write(tls, res, net::use_awaitable);
                        break;
                    }

                    ssl::stream<tcp::socket> tlsStream(std::move(tls));
                    std::lock_guard<std::mutex> lk(sseThreadsMutex);
                    sseThreads.emplace_back([this, s = std::move(tlsStream), session]() mutable {
                        try {
                            const std::string header =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/event-stream\r\n"
                                "Cache-Control: no-cache\r\n"
                                "Connection: close\r\n"
                                "Mcp-Session-Id: " + session->id + "\r\n\r\n";
                            boost::asio::write(s, boost::asio::buffer(header));
                            while (running.load()) {
                                std::pair<std::string, std::string> next;
                                {
                                    std::unique_lock<std::mutex> sessionLock(session->mutex);
                                    session->cv.wait_for(sessionLock, std::chrono::milliseconds(500), [&]() {
                                        return session->closed || !session->outboundMessages.empty() || !running.load();
                                    });
                                    if (session->closed || !running.load()) {
                                        break;
                                    }
                                    if (session->outboundMessages.empty()) {
                                        continue;
                                    }
                                    next = std::move(session->outboundMessages.front());
                                    session->outboundMessages.pop_front();
                                }
                                const std::string event = std::string("id: ") + next.first + std::string("\n") +
                                                          std::string("data: ") + next.second + std::string("\n\n");
                                boost::asio::write(s, boost::asio::buffer(event));
                            }
                        } catch (const std::exception& e) {
                            setError(std::string("HTTPServer SSE TLS session error: ") + e.what());
                        }
                        boost::system::error_code ec;
                        s.shutdown(ec);
                    });
                    co_return;
                }
                auto res = co_await coMakeResponse(std::move(req));
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

    std::string buildResourceMetadataJson() const {
        std::string json = std::string("{");
        json += std::string("\"authorization_servers\":[");
        for (size_t i = 0; i < bearerOptions.authorizationServers.size(); ++i) {
            const std::string& v = bearerOptions.authorizationServers[i];
            json += std::string("\"") + v + std::string("\"");
            if (i + 1 < bearerOptions.authorizationServers.size()) {
                json += std::string(",");
            }
        }
        json += std::string("]");
        json += std::string(",\"scopes_supported\":[");
        for (size_t i = 0; i < bearerOptions.scopesSupported.size(); ++i) {
            const std::string& v = bearerOptions.scopesSupported[i];
            json += std::string("\"") + v + std::string("\"");
            if (i + 1 < bearerOptions.scopesSupported.size()) {
                json += std::string(",");
            }
        }
        json += std::string("]}");
        return json;
    }

    //==========================================================================================================
    // enforceAuthAndScope
    // Purpose: Enforce optional Bearer authentication. On failure, populate 'res' with proper status,
    //          body, and WWW-Authenticate header when configured. Returns true when authorized.
    //==========================================================================================================
    bool enforceAuthAndScope(const http::request<http::string_body>& req,
                             http::response<http::string_body>& res,
                             mcp::auth::TokenInfo& outInfo) {
        if (!bearerAuthEnabled || tokenVerifier == nullptr) {
            return true;
        }
        const auto h = req[http::field::authorization];
        const std::string authHeader = std::string(h);
        auto r = mcp::auth::CheckBearerAuth(authHeader, *tokenVerifier, bearerOptions, outInfo);
        if (r.ok) {
            return true;
        }
        if (r.httpStatus == 403) {
            res.result(http::status::forbidden);
        } else {
            res.result(http::status::unauthorized);
        }
        if (r.includeWWWAuthenticate && !bearerOptions.resourceMetadataUrl.empty()) {
            auto joinScopes = [](const std::vector<std::string>& scopes){
                std::string s;
                for (size_t i = 0; i < scopes.size(); ++i) {
                    s += scopes[i];
                    if (i + 1 < scopes.size()) { s += std::string(" "); }
                }
                return s;
            };
            auto quote = [](const std::string& in){
                std::string out; out.reserve(in.size() + 2);
                out.push_back('"');
                for (size_t i = 0; i < in.size(); ++i) {
                    char ch = in[i];
                    if (ch == '\\' || ch == '"') { out.push_back('\\'); }
                    out.push_back(ch);
                }
                out.push_back('"');
                return out;
            };

            std::string www = std::string("Bearer resource_metadata=") + bearerOptions.resourceMetadataUrl;
            if (r.httpStatus == 403) {
                www += std::string(", error=") + quote(std::string("insufficient_scope"));
                if (!bearerOptions.requiredScopes.empty()) {
                    www += std::string(", scope=") + quote(joinScopes(bearerOptions.requiredScopes));
                }
                if (!r.errorMessage.empty()) {
                    www += std::string(", error_description=") + quote(r.errorMessage);
                }
            } else {
                if (!bearerOptions.requiredScopes.empty()) {
                    www += std::string(", scope=") + quote(joinScopes(bearerOptions.requiredScopes));
                }
            }
            res.set(http::field::www_authenticate, www);
        }
        res.body() = std::string("{\"error\":\"") + r.errorMessage + std::string("\"}");
        res.prepare_payload();
        return false;
    }

    //==========================================================================================================
    // handleRpc
    // Purpose: Handle POST to rpcPath with optional Bearer enforcement and request handler dispatch.
    //==========================================================================================================
    http::response<http::string_body> handleRpc(const http::request<http::string_body>& req) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.keep_alive(false);
        mcp::auth::TokenInfo info;
        if (!enforceAuthAndScope(req, res, info)) {
            return res;
        }
        mcp::auth::TokenInfoScope tokenScope(bearerAuthEnabled ? &info : nullptr);
        // Preserve ParseError mapping for invalid request JSON
        JSONRPCRequest rpc;
        if (!rpc.Deserialize(req.body())) {
            auto err = CreateErrorResponse(nullptr, JSONRPCErrorCodes::ParseError, "Parse error");
            res.body() = err->Serialize(); res.prepare_payload();
            return res;
        }

        RouterHandlers handlers{};
        handlers.requestHandler = requestHandler;
        handlers.notificationHandler = notificationHandler;
        handlers.errorHandler = errorHandler;

        auto resolve = [&](JSONRPCResponse&&) {
            // Unexpected inbound response on /rpc endpoint; surface via error handler only
            if (errorHandler) { errorHandler("HTTPServer: unexpected response received on /rpc"); }
        };

        auto routed = router ? router->route(req.body(), handlers, resolve) : std::optional<std::string>{};
        if (routed.has_value()) {
            res.body() = routed.value();
            res.prepare_payload();
            return res;
        }
        // Safety fallback (should not happen if request parsed successfully)
        {
            auto er = std::make_unique<JSONRPCResponse>();
            er->id = rpc.id;
            er->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Router did not produce response");
            res.body() = er->Serialize(); res.prepare_payload();
            return res;
        }
    }

    //==========================================================================================================
    // handleNotify
    // Purpose: Handle POST to notifyPath with optional Bearer enforcement and notification dispatch.
    //==========================================================================================================
    http::response<http::string_body> handleNotify(const http::request<http::string_body>& req) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.keep_alive(false);
        mcp::auth::TokenInfo info;
        if (!enforceAuthAndScope(req, res, info)) {
            return res;
        }
        mcp::auth::TokenInfoScope tokenScope(bearerAuthEnabled ? &info : nullptr);
        JSONRPCNotification note;
        if (!note.Deserialize(req.body())) {
            auto er = CreateErrorResponse(nullptr, JSONRPCErrorCodes::InvalidRequest, "Invalid notification");
            res.body() = er->Serialize(); res.prepare_payload();
            return res;
        }

        RouterHandlers handlers{};
        handlers.requestHandler = requestHandler; // not used on notify path but harmless
        handlers.notificationHandler = notificationHandler;
        handlers.errorHandler = errorHandler;

        auto resolve = [](JSONRPCResponse&&) {};
        (void)(router ? router->route(req.body(), handlers, resolve) : std::optional<std::string>{});

        res.body() = std::string("{}"); res.prepare_payload();
        return res;
    }

    http::response<http::string_body> handleEndpoint(const http::request<http::string_body>& req) {
        http::response<http::string_body> res{http::status::accepted, req.version()};
        res.set(http::field::content_type, "application/json");
        res.keep_alive(false);

        mcp::auth::TokenInfo info;
        if (!enforceAuthAndScope(req, res, info)) {
            return res;
        }
        mcp::auth::TokenInfoScope tokenScope(bearerAuthEnabled ? &info : nullptr);

        if (req.method() == http::verb::delete_) {
            const std::string sessionHeader = std::string(req["Mcp-Session-Id"]);
            if (sessionHeader.empty()) {
                res.result(http::status::bad_request);
                res.body() = std::string("{\"error\":\"Missing session id\"}");
                res.prepare_payload();
                return res;
            }
            removeSession(sessionHeader);
            res.result(http::status::no_content);
            res.body().clear();
            return res;
        }

        if (req.method() != http::verb::post) {
            res.result(http::status::method_not_allowed);
            res.body() = std::string("{\"error\":\"POST or DELETE required\"}");
            res.prepare_payload();
            return res;
        }

        JSONRPCRequest parsedRequest;
        JSONRPCNotification parsedNotification;
        JSONRPCResponse parsedResponse;
        const bool isRequest = parsedRequest.Deserialize(req.body());
        const bool isNotification = parsedNotification.Deserialize(req.body());
        const bool isResponse = parsedResponse.Deserialize(req.body());
        if (!isRequest && !isNotification && !isResponse) {
            auto err = CreateErrorResponse(nullptr, JSONRPCErrorCodes::ParseError, "Parse error");
            res.result(http::status::bad_request);
            res.body() = err->Serialize();
            res.prepare_payload();
            return res;
        }

        std::shared_ptr<SessionState> session;
        const std::string sessionHeader = std::string(req["Mcp-Session-Id"]);
        if (isRequest && parsedRequest.method == Methods::Initialize && sessionHeader.empty()) {
            std::string protocolVersion = PROTOCOL_VERSION;
            if (parsedRequest.params.has_value() &&
                std::holds_alternative<JSONValue::Object>(parsedRequest.params->value)) {
                const auto& paramsObj = std::get<JSONValue::Object>(parsedRequest.params->value);
                auto it = paramsObj.find("protocolVersion");
                if (it != paramsObj.end() && it->second &&
                    std::holds_alternative<std::string>(it->second->value)) {
                    protocolVersion = std::get<std::string>(it->second->value);
                }
            }
            session = createSession(protocolVersion);
        } else {
            session = findSession(sessionHeader);
            if (!session) {
                res.result(http::status::bad_request);
                res.body() = std::string("{\"error\":\"Missing or invalid session\"}");
                res.prepare_payload();
                return res;
            }
            if (!validateProtocolHeader(req, session, res)) {
                return res;
            }
        }

        RouterHandlers handlers{};
        handlers.requestHandler = requestHandler;
        handlers.notificationHandler = notificationHandler;
        handlers.errorHandler = errorHandler;

        auto resolve = [this, session](JSONRPCResponse&& response) {
            if (!resolvePendingResponse(session, std::move(response)) && errorHandler) {
                errorHandler("HTTPServer: unexpected response received on streamable endpoint");
            }
        };

        auto routed = router ? router->route(req.body(), handlers, resolve) : std::optional<std::string>{};
        res.set("Mcp-Session-Id", session->id);
        if (routed.has_value()) {
            res.result(http::status::ok);
            res.body() = routed.value();
            res.prepare_payload();
            return res;
        }
        if (isNotification || isResponse) {
            res.result(http::status::accepted);
            res.body().clear();
            return res;
        }
        res.body() = std::string("{}");
        res.prepare_payload();
        return res;
    }

    http::response<http::string_body> makeResponse(const http::request<http::string_body>& req) {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "application/json");
        res.keep_alive(false);

        if (!validateLocalHeaders(req, res)) {
            return res;
        }

        const std::string target = std::string(req.target());

        // Serve RFC 9728 metadata at well-known endpoints (GET)
        std::string basePath = useStreamableHttp() ? endpointPath() : opts.rpcPath;
        std::string rpcBase;
        auto lastSlash = basePath.rfind('/');
        if (lastSlash != std::string::npos) {
            if (lastSlash > 0) { rpcBase = basePath.substr(0, lastSlash); }
        }
        const std::string wellKnownRoot = std::string("/.well-known/oauth-protected-resource");
        const std::string wellKnownForBase = std::string("/.well-known/oauth-protected-resource") + (rpcBase.empty() ? std::string() : rpcBase);
        if (target == wellKnownRoot || target == wellKnownForBase) {
            if (req.method() != http::verb::get) {
                res.result(http::status::method_not_allowed);
                res.body() = std::string("{\"error\":\"GET required\"}");
                res.prepare_payload();
                return res;
            }
            res.result(http::status::ok);
            res.body() = buildResourceMetadataJson();
            res.prepare_payload();
            return res;
        }

        if (useStreamableHttp() && target == endpointPath()) {
            return handleEndpoint(req);
        }

        if (req.method() != http::verb::post) {
            res.result(http::status::bad_request);
            res.body() = std::string("{\"error\":\"POST required\"}");
            res.prepare_payload();
            return res;
        }

        if (target == opts.rpcPath) {
            return handleRpc(req);
        }

        if (target == opts.notifyPath) {
            return handleNotify(req);
        }

        res.result(http::status::not_found);
        res.body() = std::string("{\"error\":\"Not found\"}");
        res.prepare_payload();
        return res;
    }

    net::awaitable<http::response<http::string_body>> coMakeResponse(http::request<http::string_body> req) {
        auto future = std::async(std::launch::async, [this, request = std::move(req)]() mutable {
            return makeResponse(request);
        });
        auto executor = co_await net::this_coro::executor;
        net::steady_timer timer(executor);
        while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            timer.expires_after(std::chrono::milliseconds(5));
            co_await timer.async_wait(net::use_awaitable);
        }
        co_return future.get();
    }

    net::awaitable<void> acceptLoop() {
        try {
            // Validate port strictly: numeric and within [0, 65535]
            if (opts.port.empty()) {
                setError("HTTPServer invalid port: empty");
                signalReadyOrErrorOnce();
                co_return;
            }
            bool allDigits = std::all_of(opts.port.begin(), opts.port.end(), [](unsigned char ch){ return std::isdigit(ch) != 0; });
            if (!allDigits) {
                setError(std::string("HTTPServer invalid port (non-numeric): ") + opts.port);
                signalReadyOrErrorOnce();
                co_return;
            }
            unsigned long portNum = 0;
            try {
                portNum = std::stoul(opts.port);
            } catch (...) {
                setError(std::string("HTTPServer invalid port (parse failure): ") + opts.port);
                signalReadyOrErrorOnce();
                co_return;
            }
            if (portNum > 65535ul) {
                setError(std::string("HTTPServer invalid port (out of range): ") + opts.port);
                signalReadyOrErrorOnce();
                co_return;
            }
            tcp::resolver resolver(co_await net::this_coro::executor);
            auto r = co_await resolver.async_resolve(opts.address, opts.port, net::use_awaitable);
            tcp::endpoint ep = *r.begin();

            acceptor = std::make_unique<tcp::acceptor>(ioc);
            acceptor->open(ep.protocol());
            acceptor->set_option(tcp::acceptor::reuse_address(true));
            acceptor->bind(ep);
            acceptor->listen();

            signalReadyOrErrorOnce();

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
            signalReadyOrErrorOnce();
        } catch (const std::exception& e) {
            if (!running.load()) {
                // Suppress shutdown-related errors; log at DEBUG only in debug builds
                #ifdef _DEBUG
                LOG_DEBUG("HTTPServer accept suppressed during shutdown: {}", e.what());
                #endif
            } else {
                setError(std::string("HTTPServer accept error: ") + e.what());
            }
            // Ensure any waiter on Start().get() is released
            signalReadyOrErrorOnce();
        }
        co_return;
    }
};

HTTPServer::HTTPServer(const Options& opts)
    : pImpl(std::make_unique<Impl>(opts)) {}

HTTPServer::~HTTPServer() = default;

std::future<void> HTTPServer::Start() {
    pImpl->listenReadyPromise = std::promise<void>();
    pImpl->listenReadySignaled.store(false);
    auto fut = pImpl->listenReadyPromise.get_future();

    pImpl->running.store(true);
    pImpl->ioThread = std::thread([this]() {
        try {
            net::co_spawn(pImpl->ioc, pImpl->acceptLoop(), net::detached);
            pImpl->ioc.run();
        } catch (const std::exception& e) {
            if (pImpl->errorHandler) { pImpl->errorHandler(e.what()); }
            pImpl->signalReadyOrErrorOnce();
        }
    });
    return fut;
}

std::future<void> HTTPServer::Stop() {
    std::promise<void> done; auto fut = done.get_future();
    try {
        pImpl->running.store(false);
        {
            std::lock_guard<std::mutex> lk(pImpl->sessionsMutex);
            for (auto& kv : pImpl->sessions) {
                if (!kv.second) {
                    continue;
                }
                std::lock_guard<std::mutex> sessionLock(kv.second->mutex);
                kv.second->closed = true;
                for (auto& pending : kv.second->pendingRequests) {
                    auto resp = std::make_unique<JSONRPCResponse>();
                    resp->id = pending.first;
                    resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport closed", std::nullopt);
                    pending.second.set_value(std::move(resp));
                }
                kv.second->pendingRequests.clear();
                kv.second->cv.notify_all();
            }
            pImpl->sessions.clear();
            pImpl->activeSessionId.clear();
        }
        if (pImpl->acceptor) {
            boost::system::error_code ec; pImpl->acceptor->close(ec);
        }
        pImpl->ioc.stop();
        if (pImpl->ioThread.joinable()) {
            pImpl->ioThread.join();
        }
        std::lock_guard<std::mutex> lk(pImpl->sseThreadsMutex);
        for (auto& thread : pImpl->sseThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        pImpl->sseThreads.clear();
    } catch (...) {}
    done.set_value();
    return fut;
}

std::future<void> HTTPServer::Close() {
    return Stop();
}

bool HTTPServer::IsConnected() const {
    return pImpl->running.load() && !pImpl->activeSessionId.empty();
}

std::string HTTPServer::GetSessionId() const {
    return pImpl->activeSessionId;
}

std::future<std::unique_ptr<JSONRPCResponse>> HTTPServer::SendRequest(std::unique_ptr<JSONRPCRequest> request) {
    std::promise<std::unique_ptr<JSONRPCResponse>> promise;
    auto fut = promise.get_future();
    auto session = pImpl->activeSession();
    if (!session || !request) {
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = request ? request->id : nullptr;
        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "No active HTTP session", std::nullopt);
        promise.set_value(std::move(resp));
        return fut;
    }

    std::string idStr;
    std::visit([&](const auto& id) {
        using T = std::decay_t<decltype(id)>;
        if constexpr (std::is_same_v<T, std::string>) {
            if (!id.empty()) {
                idStr = id;
            }
        } else if constexpr (std::is_same_v<T, int64_t>) {
            idStr = std::to_string(id);
        }
    }, request->id);
    if (idStr.empty()) {
        idStr = pImpl->generateRequestId();
        request->id = idStr;
    }

    {
        std::lock_guard<std::mutex> lk(session->mutex);
        session->pendingRequests[idStr] = std::move(promise);
    }
    pImpl->queueOutbound(session, request->Serialize());
    return fut;
}

std::future<void> HTTPServer::SendNotification(std::unique_ptr<JSONRPCNotification> notification) {
    std::promise<void> done;
    auto fut = done.get_future();
    auto session = pImpl->activeSession();
    if (!session || !notification) {
        done.set_value();
        return fut;
    }
    pImpl->queueOutbound(session, notification->Serialize());
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

void HTTPServer::SetProtectedResourceMetadata(const mcp::auth::RequireBearerTokenOptions& opts) {
    pImpl->bearerOptions.authorizationServers = opts.authorizationServers;
    pImpl->bearerOptions.scopesSupported = opts.scopesSupported;
    pImpl->bearerOptions.resourceMetadataUrl = opts.resourceMetadataUrl;
    pImpl->bearerOptions.requiredScopes = opts.requiredScopes;
}

void HTTPServer::SetBearerAuth(mcp::auth::ITokenVerifier& verifier,
                       const mcp::auth::RequireBearerTokenOptions& opts) {
    pImpl->tokenVerifier = &verifier;
    pImpl->bearerOptions = opts;
    pImpl->bearerAuthEnabled = true;
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

    // Parse query parameters: cert, key, endpoint/stream/rpc/notify paths
    if (!query.empty()) {
        std::stringstream ss(query);
        std::string kv;
        while (std::getline(ss, kv, '&')) {
            auto eq = kv.find('=');
            std::string key = (eq == std::string::npos) ? kv : kv.substr(0, eq);
            std::string val = (eq == std::string::npos) ? std::string() : kv.substr(eq + 1);
            if (key == "cert") opts.certFile = val;
            else if (key == "key") opts.keyFile = val;
            else if (key == "endpointPath" || key == "endpoint") opts.endpointPath = val;
            else if (key == "streamPath" || key == "stream") opts.streamPath = val;
            else if (key == "rpcPath" || key == "rpc") opts.rpcPath = val;
            else if (key == "notifyPath" || key == "notify") opts.notifyPath = val;
        }
    }

    // Return server acceptor
    return std::unique_ptr<ITransportAcceptor>(new HTTPServer(opts));
}

} // namespace mcp
