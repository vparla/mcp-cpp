//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Transport.h
// Purpose: MCP transport layer interfaces - COM-style abstractions for connection management
//==========================================================================================================

#pragma once

#include <memory>
#include <string>
#include <functional>
#include <future>
#include <cstdint>

namespace mcp {

// Forward declarations
class JSONRPCRequest;
class JSONRPCResponse;
class JSONRPCNotification;

//==========================================================================================================
// MCP Transport interface
// Purpose: MCP transport interface definitions.
//==========================================================================================================
class ITransport {
public:
    virtual ~ITransport() = default;

    /////////////////////////////////////////// Connection lifecycle ///////////////////////////////////////////
    //==========================================================================================================
    // Starts the transport I/O loop.
    // Args:
    //   (none)
    // Returns:
    //   A future that completes when the transport is running.
    //==========================================================================================================
    virtual std::future<void> Start() = 0;

    //==========================================================================================================
    // Closes the transport and releases resources.
    // Args:
    //   (none)
    // Returns:
    //   A future that completes when the transport has closed.
    //==========================================================================================================
    virtual std::future<void> Close() = 0;

    //==========================================================================================================
    // Indicates whether the transport is currently connected.
    // Args:
    //   (none)
    // Returns:
    //   true if connected; false otherwise.
    //==========================================================================================================
    virtual bool IsConnected() const = 0;

    //==========================================================================================================
    // Returns a transport session identifier for diagnostics.
    // Args:
    //   (none)
    // Returns:
    //   A string identifying the current session.
    //==========================================================================================================
    virtual std::string GetSessionId() const = 0;

    /////////////////////////////////////////// Message sending ///////////////////////////////////////////
    //==========================================================================================================
    // Sends a JSON-RPC request and returns a future for the response.
    // Args:
    //   request: Unique pointer to a JSONRPCRequest to send.
    // Returns:
    //   Future resolving to a unique_ptr<JSONRPCResponse> (or error encoded in response).
    //==========================================================================================================
    virtual std::future<std::unique_ptr<JSONRPCResponse>> SendRequest(
        std::unique_ptr<JSONRPCRequest> request) = 0;

    //==========================================================================================================
    // Sends a JSON-RPC notification (no response expected).
    // Args:
    //   notification: Unique pointer to a JSONRPCNotification to send.
    // Returns:
    //   Future completing when the notification has been written to the transport.
    //==========================================================================================================
    virtual std::future<void> SendNotification(
        std::unique_ptr<JSONRPCNotification> notification) = 0;

    /////////////////////////////////////////// Notification handling ///////////////////////////////////////////
    //==========================================================================================================
    // Registers a callback for incoming notifications.
    // Args:
    //   handler: Callback invoked with ownership of the incoming notification object.
    // Returns:
    //   (none)
    //==========================================================================================================
    using NotificationHandler = std::function<void(std::unique_ptr<JSONRPCNotification>)>;
    virtual void SetNotificationHandler(NotificationHandler handler) = 0;

    /////////////////////////////////////////// Request handling ///////////////////////////////////////////
    //==========================================================================================================
    // Registers a server-side request handler; transport invokes it on incoming requests
    // and is responsible for sending back the returned response to the peer.
    // Args:
    //   handler: Callback that takes a const JSONRPCRequest& and returns a unique_ptr<JSONRPCResponse>.
    // Returns:
    //   (none)
    //==========================================================================================================
    using RequestHandler = std::function<std::unique_ptr<JSONRPCResponse>(const JSONRPCRequest&)>;
    virtual void SetRequestHandler(RequestHandler handler) = 0;

    /////////////////////////////////////////// Error handling ///////////////////////////////////////////
    //==========================================================================================================
    // Registers an error handler to receive transport errors.
    // Args:
    //   handler: Callback with error string.
    // Returns:
    //   (none)
    //==========================================================================================================
    using ErrorHandler = std::function<void(const std::string& error)>;
    virtual void SetErrorHandler(ErrorHandler handler) = 0;
};

// Concrete transports are declared in their respective headers:
//  - mcp/StdioTransport.hpp
//  - mcp/InMemoryTransport.hpp

//==========================================================================================================
// Transport factory interface
// Purpose: Factory for creating transports from configuration strings.
//==========================================================================================================
class ITransportFactory {
public:
    virtual ~ITransportFactory() = default;

    //==========================================================================================================
    // Creates a transport instance using the provided configuration.
    // Args:
    //   config: Transport-specific configuration string (e.g., "stdio" settings or FIFO paths).
    // Returns:
    //   A unique_ptr to a newly created ITransport.
    //==========================================================================================================
    virtual std::unique_ptr<ITransport> CreateTransport(const std::string& config) = 0;
};

} // namespace mcp
