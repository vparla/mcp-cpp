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
//  - mcp/HTTPTransport.hpp
//  - mcp/HTTPServer.hpp (implements ITransportAcceptor)

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

//==========================================================================================================
// ITransportAcceptor
// Purpose: Server-side acceptor interface which owns the listen lifecycle and dispatches incoming
//          JSON-RPC requests/notifications to registered handlers.
// Notes:
//   - Keeps the multi-client accept/listen role separate from the single-session ITransport.
//   - Implementations should bind/listen in Start(), stop/teardown in Stop(), and invoke the
//     registered handlers upon incoming messages.
//   - This interface mirrors the handler signatures of ITransport for consistency.
//==========================================================================================================
class ITransportAcceptor {
public:
    virtual ~ITransportAcceptor() = default;

    //==========================================================================================================
    // Starts the acceptor (binds/listens/spawns accept loop as needed).
    // Args:
    //   (none)
    // Returns:
    //   Future that completes when the accept loop is running.
    //==========================================================================================================
    virtual std::future<void> Start() = 0;

    //==========================================================================================================
    // Stops the acceptor and releases resources (closes listener and active sessions gracefully).
    // Args:
    //   (none)
    // Returns:
    //   Future that completes when the acceptor has stopped.
    //==========================================================================================================
    virtual std::future<void> Stop() = 0;

    //==========================================================================================================
    // Registers the server-side JSON-RPC request handler. Invoked per request, must return a response.
    // Args:
    //   handler: Callback taking const JSONRPCRequest& and returning unique_ptr<JSONRPCResponse>.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void SetRequestHandler(ITransport::RequestHandler handler) = 0;

    //==========================================================================================================
    // Registers the JSON-RPC notification handler (no response expected).
    // Args:
    //   handler: Callback invoked with ownership of the incoming notification object.
    // Returns:
    //   (none)
    //==========================================================================================================
    virtual void SetNotificationHandler(ITransport::NotificationHandler handler) = 0;

    //==========================================================================================================
    // Registers an error handler to receive acceptor/transport errors.
    // Args:
    //   handler: Callback receiving error strings.
    virtual void SetErrorHandler(ITransport::ErrorHandler handler) = 0;
};

//==========================================================================================================
// Transport acceptor factory interface
// Purpose: Factory for creating server-side acceptors from configuration strings.
//==========================================================================================================
  class ITransportAcceptorFactory {
  public:
    virtual ~ITransportAcceptorFactory() = default;

    //==========================================================================================================
    // Creates a server-side acceptor instance using the provided configuration.
    // Args:
    //   config: Transport-specific configuration string (e.g., "http://127.0.0.1:9443").
    // Returns:
    //   A unique_ptr to a newly created ITransportAcceptor.
    //==========================================================================================================
    virtual std::unique_ptr<ITransportAcceptor> CreateTransportAcceptor(const std::string& config) = 0;
  };

} // namespace mcp
