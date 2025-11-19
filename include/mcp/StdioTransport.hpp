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

//==========================================================================================================
// StdioTransport
// Purpose: JSON-RPC transport over stdin/stdout suitable for local tool integrations.
//==========================================================================================================
class StdioTransport : public ITransport {
public:
    StdioTransport();
    virtual ~StdioTransport();

    ////////////////////////////////////////// ITransport //////////////////////////////////////////
    //==========================================================================================================
    // Starts the stdio transport reader/writer loops.
    // Returns:
    //   Future that completes when loops are running.
    //==========================================================================================================
    std::future<void> Start() override;

    //==========================================================================================================
    // Closes the transport and stops reader/writer loops.
    // Returns:
    //   Future that completes when closed.
    //==========================================================================================================
    std::future<void> Close() override;

    //==========================================================================================================
    // Indicates whether transport is connected.
    //==========================================================================================================
    bool IsConnected() const override;

    //==========================================================================================================
    // Returns a diagnostic session identifier.
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
    // Registers handlers for incoming notifications, requests (if supported), and errors.
    //==========================================================================================================
    void SetNotificationHandler(NotificationHandler handler) override;
    void SetRequestHandler(RequestHandler handler) override;
    void SetErrorHandler(ErrorHandler handler) override;

    //==========================================================================================================
    // SetRequestTimeoutMs
    // Purpose: Configure maximum time to wait for a single request/response pair.
    // Args:
    //   timeoutMs: Timeout in milliseconds (default ~30000 when not set).
    //==========================================================================================================
    void SetRequestTimeoutMs(uint64_t timeoutMs);

    //==========================================================================================================
    // SetIdleReadTimeoutMs
    // Purpose: If > 0, emit an error and close when no bytes arrive for the given duration.
    // Args:
    //   timeoutMs: Idle read timeout in milliseconds (0 disables idle timeout).
    //==========================================================================================================
    void SetIdleReadTimeoutMs(uint64_t timeoutMs);

    //==========================================================================================================
    // SetWriteQueueMaxBytes
    // Purpose: Backpressure clamp for pending write buffers.
    // Args:
    //   maxBytes: Maximum allowed bytes in write queue before emitting an error and closing.
    //==========================================================================================================
    void SetWriteQueueMaxBytes(std::size_t maxBytes);

    //==========================================================================================================
    // SetWriteTimeoutMs
    // Purpose: Per-frame write timeout.
    // Args:
    //   timeoutMs: Milliseconds to allow for writing a frame before error/close.
    //==========================================================================================================
    void SetWriteTimeoutMs(uint64_t timeoutMs);

    void SetMaxContentLength(std::size_t maxBytes);

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
    friend struct StdioTransportTestHooks;
};

//==========================================================================================================
// StdioTransportFactory
// Purpose: Factory for creating stdio transports.
//==========================================================================================================
class StdioTransportFactory : public ITransportFactory {
public:
    std::unique_ptr<ITransport> CreateTransport(const std::string& config) override;
};

struct StdioTransportTestHooks {
    static void drainFrames(StdioTransport& t, std::string& buffer);
    static void setConnected(StdioTransport& t, bool v);
    static bool isConnected(const StdioTransport& t);
};

} // namespace mcp
