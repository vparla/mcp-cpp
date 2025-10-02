//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: include/mcp/HTTPTransport.hpp
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

class HTTPTransport : public ITransport {
public:
    struct Options {
        std::string scheme{"https"};   // "http" or "https"
        std::string host{"localhost"};
        std::string port{"9443"};
        std::string rpcPath{"/mcp/rpc"};
        std::string notifyPath{"/mcp/notify"};
        std::string serverName;  // SNI / hostname verification for TLS
        std::string caFile;      // optional
        std::string caPath;      // optional
        unsigned int connectTimeoutMs{10000};
        unsigned int readTimeoutMs{30000};
    };

    explicit HTTPTransport(const Options& opts);
    ~HTTPTransport() override;

    // ITransport
    std::future<void> Start() override;
    std::future<void> Close() override;
    bool IsConnected() const override;
    std::string GetSessionId() const override;

    std::future<std::unique_ptr<JSONRPCResponse>> SendRequest(
        std::unique_ptr<JSONRPCRequest> request) override;
    std::future<void> SendNotification(
        std::unique_ptr<JSONRPCNotification> notification) override;

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
