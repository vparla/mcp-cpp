//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: include/mcp/HTTPServer.hpp
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

    std::future<void> Start();
    std::future<void> Stop();

    void SetRequestHandler(RequestHandler handler);
    void SetNotificationHandler(NotificationHandler handler);
    void SetErrorHandler(ErrorHandler handler);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace mcp
