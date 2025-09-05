<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: docs/api/transport.md
Purpose: Transport API reference (ITransport / ITransportFactory)
==========================================================================================================
-->
# Transport API (ITransport / ITransportFactory)

Header: [include/mcp/Transport.h](../../include/mcp/Transport.h)

Transports provide JSON-RPC message delivery and connection lifecycle. Concrete implementations:
- In-memory: [include/mcp/InMemoryTransport.hpp](../../include/mcp/InMemoryTransport.hpp)
- Stdio: [include/mcp/StdioTransport.hpp](../../include/mcp/StdioTransport.hpp)

## ITransport
- std::future<void> Start()
  - Starts the transport I/O loop.
- std::future<void> Close()
  - Closes the transport and releases resources.
- bool IsConnected() const
  - Returns true if the transport is connected.
- std::string GetSessionId() const
  - Returns a session identifier for diagnostics.

### Message sending
- std::future<std::unique_ptr<JSONRPCResponse>> SendRequest(std::unique_ptr<JSONRPCRequest> request)
  - Sends a JSON-RPC request and resolves to the response.
- std::future<void> SendNotification(std::unique_ptr<JSONRPCNotification> notification)
  - Sends a JSON-RPC notification (no response expected).

### Handlers
- using NotificationHandler = std::function<void(std::unique_ptr<JSONRPCNotification>)>;
- void SetNotificationHandler(NotificationHandler handler)
  - Receives incoming notifications.
- using RequestHandler = std::function<std::unique_ptr<JSONRPCResponse>(const JSONRPCRequest&)>;
- void SetRequestHandler(RequestHandler handler)
  - Server-side hook to handle incoming requests; return value is sent back to the peer.
- using ErrorHandler = std::function<void(const std::string& error)>;
- void SetErrorHandler(ErrorHandler handler)
  - Receives transport-level errors.

## ITransportFactory
- std::unique_ptr<ITransport> CreateTransport(const std::string& config)
  - Creates a transport instance (e.g., stdio config). See concrete factory headers for parameters.
