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

#include "mcp/Transport.h"

namespace mcp {

//==========================================================================================================
// HTTPTransport
// Purpose: Concrete HTTP/HTTPS transport implementing ITransport using Boost.Beast coroutines.
//==========================================================================================================
class HTTPTransport : public ITransport {
public:
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

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

class HTTPTransportFactory : public ITransportFactory {
public:
    std::unique_ptr<ITransport> CreateTransport(const std::string& config) override;
};

} // namespace mcp
