//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: HTTPTransport.hpp
// Purpose: Coroutine-based HTTP/HTTPS JSON-RPC client transport using Boost.Beast (TLS 1.3 only for HTTPS)
//==========================================================================================================

#pragma once

#include <memory>
#include <string>
#include <future>
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

#include "mcp/Transport.h"
#include "mcp/auth/IAuth.hpp"

namespace mcp {

//==========================================================================================================
// HTTPTransport
// Purpose: Concrete HTTP/HTTPS transport implementing ITransport using Boost.Beast coroutines.
//==========================================================================================================
class HTTPTransport : public ITransport {
public:
    //==========================================================================================================
    // HttpResponseInfo
    // Purpose: Exposes HTTP status and headers from the last completed HTTP request. Used for auth discovery
    //          (e.g., parsing WWW-Authenticate on 401/403).
    //==========================================================================================================
    struct HttpResponseInfo {
        int status{0};
        std::vector<mcp::auth::HeaderKV> headers;
        std::string wwwAuthenticate; // convenience copy of WWW-Authenticate if present
    };
    
    //==========================================================================================================
    // Options
    // Purpose: Configuration for HTTP/HTTPS endpoints and TLS verification.
    // Fields:
    //   scheme: "http" or "https" (default: https)
    //   host: Server hostname or IP (default: localhost)
    //   port: Service port (default: 9443)
    //   rpcPath: JSON-RPC request path
    //   notifyPath: JSON-RPC notification path
    //   serverName: TLS SNI and hostname verification name (when https)
    //   caFile/caPath: Optional CA bundle/path for trust store
    //   connectTimeoutMs: Connect timeout in milliseconds
    //   readTimeoutMs: Read timeout in milliseconds
    //==========================================================================================================
    struct Options {
        std::string scheme{"https"};
        std::string host{"localhost"};
        std::string port{"9443"};
        std::string rpcPath{"/mcp/rpc"};
        std::string notifyPath{"/mcp/notify"};
        std::string serverName;
        std::string caFile;
        std::string caPath;
        unsigned int connectTimeoutMs{10000};
        unsigned int readTimeoutMs{30000};

        //======================================================================================================
        // Authentication (optional)
        // Purpose: Configure HTTP Authorization header injection and OAuth2 client-credentials flow.
        // Fields:
        //   auth: "none" | "bearer" | "oauth2". Default: "none" (no Authorization header added).
        //   bearerToken: Static Bearer token value when auth=="bearer".
        //   oauthTokenUrl: Full URL to the OAuth2 token endpoint (e.g., https://auth.example.com/oauth2/token).
        //   clientId/clientSecret: OAuth2 client credentials for client-credentials grant.
        //   scope: Optional space-delimited scopes for the token request.
        //   tokenRefreshSkewSeconds: Seconds to subtract from expires_in to proactively refresh before expiry.
        //======================================================================================================
        std::string auth{"none"};
        std::string bearerToken;
        std::string oauthTokenUrl;
        std::string clientId;
        std::string clientSecret;
        std::string scope;
        unsigned int tokenRefreshSkewSeconds{60};
    };

    explicit HTTPTransport(const Options& opts);
    ~HTTPTransport() override;

    ////////////////////////////////////////// ITransport //////////////////////////////////////////
    //==========================================================================================================
    // Starts the transport I/O loop. Returns when worker is ready to accept requests.
    //==========================================================================================================
    std::future<void> Start() override;

    //==========================================================================================================
    // Closes the transport and stops the I/O loop.
    //==========================================================================================================
    std::future<void> Close() override;

    //==========================================================================================================
    // Indicates whether the transport session is currently connected.
    //==========================================================================================================
    bool IsConnected() const override;

    //==========================================================================================================
    // Returns a transport session identifier for diagnostics.
    //==========================================================================================================
    std::string GetSessionId() const override;

    //==========================================================================================================
    // Sends a JSON-RPC request and returns a future for the response.
    //==========================================================================================================
    std::future<std::unique_ptr<JSONRPCResponse>> SendRequest(
        std::unique_ptr<JSONRPCRequest> request) override;
        
    //==========================================================================================================
    // Sends a JSON-RPC notification (no response expected).
    //==========================================================================================================
    std::future<void> SendNotification(
        std::unique_ptr<JSONRPCNotification> notification) override;

    //==========================================================================================================
    // Registers handlers for incoming notifications, requests (unused for client), and errors.
    //==========================================================================================================
    void SetNotificationHandler(NotificationHandler handler) override;
    void SetRequestHandler(RequestHandler handler) override;
    void SetErrorHandler(ErrorHandler handler) override;
    void SetAuth(mcp::auth::IAuth& auth);
    void SetAuth(std::shared_ptr<mcp::auth::IAuth> auth);

    //==========================================================================================================
    // TryGetLastHttpResponse
    // Purpose: Copies the last observed HTTP response info (if any). Returns true on success.
    //          Thread-safe; returns a snapshot and does not clear the stored value.
    //==========================================================================================================
    bool QueryLastHttpResponse(HttpResponseInfo& out) const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

//==========================================================================================================
// HTTPTransportFactory
// Purpose: Factory for creating HTTP/HTTPS transports.
//==========================================================================================================
class HTTPTransportFactory : public ITransportFactory {
public:
    std::unique_ptr<ITransport> CreateTransport(const std::string& config) override;
};

} // namespace mcp
