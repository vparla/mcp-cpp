//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: StdioTransport.hpp
// Purpose: Concrete stdio-based transport
//==========================================================================================================
#pragma once

#include "mcp/Transport.h"
#include <memory>
#include <cstdint>

namespace mcp {

class StdioTransport : public ITransport {
public:
    StdioTransport();
    virtual ~StdioTransport();

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
    void SetRequestHandler(RequestHandler handler) override;
    void SetErrorHandler(ErrorHandler handler) override;

    // Optional: configure request timeout (milliseconds). Defaults to 30000 ms.
    void SetRequestTimeoutMs(uint64_t timeoutMs);

    // Optional: configure idle read timeout (milliseconds). If > 0, transport will emit an error
    // and close when no bytes are received for the given duration.
    void SetIdleReadTimeoutMs(uint64_t timeoutMs);

    // Optional: configure maximum bytes allowed in the write queue for backpressure.
    // When exceeded, transport will emit an error and close.
    void SetWriteQueueMaxBytes(std::size_t maxBytes);

    // Optional: configure per-frame write timeout (milliseconds).
    // When a frame cannot be fully written within this time, transport emits an error and closes.
    void SetWriteTimeoutMs(uint64_t timeoutMs);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

class StdioTransportFactory : public ITransportFactory {
public:
    std::unique_ptr<ITransport> CreateTransport(const std::string& config) override;
};

} // namespace mcp
