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

#include "mcp/JSONRPCTypes.h"

namespace mcp {

class HTTPServer {
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

    using RequestHandler = std::function<std::unique_ptr<JSONRPCResponse>(const JSONRPCRequest&)>;
    using NotificationHandler = std::function<void(std::unique_ptr<JSONRPCNotification>)>;
    using ErrorHandler   = std::function<void(const std::string&)>;

    explicit HTTPServer(const Options& opts);
    ~HTTPServer();

    //==========================================================================================================
    // Starts the server accept loop on a background I/O thread.
    // Returns:
    //   Future that becomes ready once the I/O context is running.
    //==========================================================================================================
    std::future<void> Start();

    //==========================================================================================================
    // Stops the server: closes acceptor, stops I/O context, and joins background thread.
    // Returns:
    //   Future that completes when shutdown has finished.
    //==========================================================================================================
    std::future<void> Stop();

    //==========================================================================================================
    // Sets the request handler (JSON-RPC request -> response).
    // Args:
    //   handler: Callback invoked per request; may return an error response.
    //==========================================================================================================
    void SetRequestHandler(RequestHandler handler);

    //==========================================================================================================
    // Sets the notification handler (no response expected).
    // Args:
    //   handler: Callback invoked per notification with ownership of the notification object.
    //==========================================================================================================
    void SetNotificationHandler(NotificationHandler handler);
    
    //==========================================================================================================
    // Sets the error handler for transport/server errors.
    // Args:
    //   handler: Callback invoked with error strings.
    //==========================================================================================================
    void SetErrorHandler(ErrorHandler handler);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace mcp
