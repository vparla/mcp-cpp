//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: HTTPServer.hpp
// Purpose: Coroutine-based HTTP/HTTPS JSON-RPC server using Boost.Beast (TLS 1.3 only for HTTPS)
//==========================================================================================================

#pragma once

#include <string>
#include <future>
#include <functional>
#include <memory>
#include "mcp/Transport.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/auth/ServerAuth.hpp"

namespace mcp {

  // Note: HTTPServer implements the server-side acceptor role (ITransportAcceptor)
  class HTTPServer : public ITransportAcceptor {
  public:
    //==========================================================================================================
    // Options
    // Purpose: Configuration for bind address/port, JSON-RPC paths, and TLS files.
    // Fields:
    //   address: Bind address (default: 0.0.0.0)
    //   port: Listen port (default: 9443)
    //   rpcPath: JSON-RPC request path
    //   notifyPath: JSON-RPC notification path
    //   scheme: "http" or "https" (TLS 1.3 only for https)
    //   certFile/keyFile: PEM files required when scheme == https
    //==========================================================================================================
    struct Options {
        std::string address{"0.0.0.0"};
        std::string port{"9443"};
        std::string rpcPath{"/mcp/rpc"};
        std::string notifyPath{"/mcp/notify"};
        std::string scheme{"https"}; // "http" or "https"
        std::string certFile; // PEM (required for https)
        std::string keyFile;  // PEM (required for https)
    };

    explicit HTTPServer(const Options& opts);
    ~HTTPServer();

    //==========================================================================================================
    // Starts the server accept loop on a background I/O thread.
    // Returns:
    //   Future that becomes ready once the I/O context is running.
    //==========================================================================================================
    std::future<void> Start() override;

    //==========================================================================================================
    // Stops the server: closes acceptor, stops I/O context, and joins background thread.
    // Returns:
    //   Future that completes when shutdown has finished.
    //==========================================================================================================
    std::future<void> Stop() override;

    //==========================================================================================================
    // Sets the request handler (JSON-RPC request -> response).
    // Args:
    //   handler: Callback invoked per request; may return an error response.
    //==========================================================================================================
    void SetRequestHandler(ITransport::RequestHandler handler) override;

    //==========================================================================================================
    // Sets the notification handler (no response expected).
    // Args:
    //   handler: Callback invoked per notification with ownership of the notification object.
    //==========================================================================================================
    void SetNotificationHandler(ITransport::NotificationHandler handler) override;
    
    //==========================================================================================================
    // Sets the error handler for transport/server errors.
    // Args:
    //   handler: Callback invoked with error strings.
    //==========================================================================================================
    void SetErrorHandler(ITransport::ErrorHandler handler) override;

    //==========================================================================================================
    // SetBearerAuth
    // Purpose: Configure optional server-side Bearer authentication for HTTP requests.
    // Notes:
    //   - Non-owning reference to an ITokenVerifier implementation; caller must ensure lifetime spans server use.
    //   - When configured, incoming requests to rpcPath/notifyPath must include a valid
    //     Authorization: Bearer <token> header. On failure, the server responds with 401/403 and
    //     includes a WWW-Authenticate: Bearer resource_metadata=<url> header when a URL is provided.
    //   - On success, the verified TokenInfo is made available for the duration of request handling
    //     via mcp::auth::CurrentTokenInfo().
    // Args:
    //   verifier: Token verifier to validate bearer tokens.
    //   opts: Required scopes and resource metadata URL for WWW-Authenticate.
    //==========================================================================================================
    void SetBearerAuth(mcp::auth::ITokenVerifier& verifier,
                       const mcp::auth::RequireBearerTokenOptions& opts);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
  };

  //==========================================================================================================
  // HTTPServerFactory
  // Purpose: Factory for creating HTTP/HTTPS server acceptors (ITransportAcceptor) from a configuration
  //          string. The configuration format is uri format, intentionally simple and stable:
  //            - "http://<address>:<port>" (e.g., http://127.0.0.1:0)
  //            - "https://<address>:<port>?cert=<pem>&key=<pem>"
  //          Unknown parameters are ignored. If scheme is omitted, defaults to http.
  //==========================================================================================================
  class HTTPServerFactory : public ITransportAcceptorFactory {
  public:
    std::unique_ptr<ITransportAcceptor> CreateTransportAcceptor(const std::string& config) override;
  };

} // namespace mcp
