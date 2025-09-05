//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: InMemoryTransport.hpp
// Purpose: In-memory transport for tests and embedding
//==========================================================================================================
#pragma once

#include "mcp/Transport.h"
#include <memory>
#include <utility>

namespace mcp {

class InMemoryTransport : public ITransport {
public:
    InMemoryTransport();
    virtual ~InMemoryTransport();

    // Create paired transports for testing
    static std::pair<std::unique_ptr<InMemoryTransport>, std::unique_ptr<InMemoryTransport>> CreatePair();

    // ITransport implementation
    std::future<void> Start() override;
    std::future<void> Close() override;
    bool IsConnected() const override;
    std::string GetSessionId() const override;

    std::future<std::unique_ptr<JSONRPCResponse>> SendRequest(
        std::unique_ptr<JSONRPCRequest> request) override;
    std::future<void> SendNotification(
        std::unique_ptr<JSONRPCNotification> notification) override;

    void SetNotificationHandler(NotificationHandler handler) override;
    void SetErrorHandler(ErrorHandler handler) override;
    void SetRequestHandler(RequestHandler handler) override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

class InMemoryTransportFactory : public ITransportFactory {
public:
    std::unique_ptr<ITransport> CreateTransport(const std::string& config) override;
};

} // namespace mcp
