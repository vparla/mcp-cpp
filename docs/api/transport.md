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

### StdioTransport factory and configuration

Header: [include/mcp/StdioTransport.hpp](../../include/mcp/StdioTransport.hpp)

The stdio transport can be created via `StdioTransportFactory::CreateTransport(config)` where `config` is a `key=value` list separated by `;` or whitespace. Recognized keys:

- `timeout_ms` — request timeout for in-flight JSON-RPC requests. `0` disables request timeouts.
- `idle_read_timeout_ms` — closes the transport if no bytes are received for this duration while connected.
- `write_timeout_ms` — maximum duration for non-blocking writes to progress; exceeded time closes the transport.
- `write_queue_max_bytes` — upper bound on buffered outgoing frames; overflow emits error and closes the transport.

Example:

```cpp
#include "mcp/StdioTransport.hpp"

mcp::StdioTransportFactory f;
auto transport = f.CreateTransport("timeout_ms=30000; idle_read_timeout_ms=200; write_timeout_ms=2000; write_queue_max_bytes=1048576");
```

Programmatic setters are also available on `StdioTransport`:

- `SetRequestTimeoutMs(uint64_t ms)`
- `SetIdleReadTimeoutMs(uint64_t ms)`
- `SetWriteTimeoutMs(uint64_t ms)`
- `SetWriteQueueMaxBytes(std::size_t bytes)`

### Environment variables

- `MCP_STDIOTRANSPORT_TIMEOUT_MS` — default request timeout in milliseconds. See usage in [src/mcp/StdioTransport.cpp](../../src/mcp/StdioTransport.cpp).
- `MCP_STDIO_CONFIG` — demo/server env to pass the same `key=value` list as the factory config. See examples in [BUILD+TEST.MD](../../BUILD+TEST.MD#demo-and-transport-options-env--factory-config).

### Hardening behaviors (stdio)

The stdio transport includes defensive behaviors to ensure robustness. Implementation lives in [src/mcp/StdioTransport.cpp](../../src/mcp/StdioTransport.cpp):

- Idle read timeout triggers close when no bytes arrive for `idle_read_timeout_ms`.
- Write queue overflow (exceeding `write_queue_max_bytes`) emits a transport error and transitions to disconnected.
- Write timeout (no forward progress for `write_timeout_ms`) emits a transport error and closes.
- Malformed frames (e.g., excessive `Content-Length`, header/body mismatch) are rejected and logged; the transport terminates.

Negative-path scripts for these behaviors are exercised by CTest targets defined in [tests/CMakeLists.txt](../../tests/CMakeLists.txt) via [scripts/test_stdio_hardening.sh](../../scripts/test_stdio_hardening.sh).

### Platform-specific notes

- Linux: uses `epoll` with a wake eventfd to interrupt waits; non-blocking I/O for stdin/stdout.
- macOS/*BSD: uses `poll` plus a self-pipe for wakeups; non-blocking I/O for stdin/stdout.
- Windows: uses `WaitForMultipleObjects` on a manual-reset event to wake the reader; `PeekNamedPipe` for readiness; overlapped semantics are avoided to keep the implementation minimal.
