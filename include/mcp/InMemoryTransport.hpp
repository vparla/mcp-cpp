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

//==========================================================================================================
// InMemoryTransport
// Purpose: Zero-copy, in-process transport used for tests and embedding. Implements ITransport and
//          delivers messages to a paired transport instance without networking or I/O.
//==========================================================================================================
class InMemoryTransport : public ITransport {
public:
    InMemoryTransport();
    virtual ~InMemoryTransport();

    //==========================================================================================================
    // CreatePair
    // Purpose: Creates two paired transports wired to each other in-memory.
    // Returns:
    //   pair(left,right) where sending on one delivers to the other.
    //==========================================================================================================
    static std::pair<std::unique_ptr<InMemoryTransport>, std::unique_ptr<InMemoryTransport>> CreatePair();

    ////////////////////////////////////////// ITransport //////////////////////////////////////////
    //==========================================================================================================
    // Starts the in-memory transport (no-op wiring setup).
    // Returns:
    //   Future that completes when ready to send/receive.
    //==========================================================================================================
    std::future<void> Start() override;

    //==========================================================================================================
    // Closes the in-memory transport and fails any pending requests.
    // Returns:
    //   Future that completes when closed.
    //==========================================================================================================
    std::future<void> Close() override;

    //==========================================================================================================
    // Indicates whether this transport is logically connected to its peer.
    //==========================================================================================================
    bool IsConnected() const override;

    //==========================================================================================================
    // Returns a diagnostic session identifier.
    //==========================================================================================================
    std::string GetSessionId() const override;

    //==========================================================================================================
    // Sends a JSON-RPC request to the paired transport and returns its response.
    //==========================================================================================================
    std::future<std::unique_ptr<JSONRPCResponse>> SendRequest(
        std::unique_ptr<JSONRPCRequest> request) override;
        
    //==========================================================================================================
    // Sends a JSON-RPC notification to the paired transport.
    //==========================================================================================================
    std::future<void> SendNotification(
        std::unique_ptr<JSONRPCNotification> notification) override;

    //==========================================================================================================
    // Registers handlers for incoming notifications and errors.
    //==========================================================================================================
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
