//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: SharedMemoryTransport.hpp
// Purpose: Cross-process shared-memory JSON-RPC transport built on Boost.Interprocess
//==========================================================================================================

#pragma once

#include <future>
#include <memory>
#include <string>
#include "mcp/Transport.h"

namespace mcp {

class SharedMemoryTransport : public ITransport {
public:
    //==========================================================================================================
    // Options
    // Purpose: Configuration for channel identity and queue sizing.
    // Fields:
    //   channelName: Base channel name. Two queues are derived: "<channelName>_c2s" and "<channelName>_s2c".
    //   create: When true, create queues (server-side). When false, open existing queues (client-side).
    //   maxMessageSize: Maximum size per message in bytes when creating queues (default: 1 MiB).
    //   maxMessageCount: Maximum number of messages buffered per queue when creating (default: 256).
    //==========================================================================================================
    struct Options {
        std::string channelName;
        bool create{false};
        std::size_t maxMessageSize{1024ull * 1024ull};
        unsigned int maxMessageCount{256u};
    };

    explicit SharedMemoryTransport(const Options& opts);
    ~SharedMemoryTransport() override;

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
    // Registers handlers for incoming notifications and errors. RequestHandler is used when this
    // transport instance is wired on the server side.
    //==========================================================================================================
    void SetNotificationHandler(NotificationHandler handler) override;
    void SetRequestHandler(RequestHandler handler) override;
    void SetErrorHandler(ErrorHandler handler) override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

//==========================================================================================================
// SharedMemoryTransportFactory
// Purpose: Factory for creating shared-memory transports from a configuration string.
//          Supported formats:
//            - "shm://<channelName>?create=true&maxSize=<bytes>&maxCount=<n>"
//            - "<channelName>" (defaults to create=false)
//          Unknown parameters are ignored.
//==========================================================================================================
class SharedMemoryTransportFactory : public ITransportFactory {
public:
    std::unique_ptr<ITransport> CreateTransport(const std::string& config) override;
};

} // namespace mcp
