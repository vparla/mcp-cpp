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
- Shared memory: [include/mcp/SharedMemoryTransport.hpp](../../include/mcp/SharedMemoryTransport.hpp)
- HTTP/HTTPS client: [include/mcp/HTTPTransport.hpp](../../include/mcp/HTTPTransport.hpp)
- HTTP/HTTPS server acceptor: [include/mcp/HTTPServer.hpp](../../include/mcp/HTTPServer.hpp)

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

## Transport types and factory usage

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

### InMemoryTransport factory

Header: [include/mcp/InMemoryTransport.hpp](../../include/mcp/InMemoryTransport.hpp)

The in-memory transport is primarily for tests and in-process demos. It supports creating paired endpoints or a single endpoint via the factory.

Examples:

```cpp
#include "mcp/InMemoryTransport.hpp"

// Paired endpoints (test/demo)
auto pair = mcp::InMemoryTransport::CreatePair();
auto client = std::move(pair.first);
auto server = std::move(pair.second);
(void)client->Start().get();
(void)server->Start().get();

// Factory-created single endpoint
mcp::InMemoryTransportFactory f;
auto t = f.CreateTransport(""); // no config required
```

### SharedMemoryTransport factory and configuration

Header: [include/mcp/SharedMemoryTransport.hpp](../../include/mcp/SharedMemoryTransport.hpp)

The shared-memory transport enables cross-process JSON-RPC using Boost.Interprocess queues. Two queues are created per channel: `<channel>_c2s` and `<channel>_s2c`.

Config formats:

- `shm://<channelName>?create=true&maxSize=<bytes>&maxCount=<n>` (creator/server side)
- `<channelName>` (peer/client side; `create=false` by default)

Examples:

```cpp
#include "mcp/SharedMemoryTransport.hpp"

// Server side (creator)
mcp::SharedMemoryTransportFactory f;
auto serverT = f.CreateTransport("shm://mcp-shm?create=true&maxSize=65536&maxCount=64");

// Client side
auto clientT = f.CreateTransport("mcp-shm");
```

### HTTP/HTTPS client transport factory

Header: [include/mcp/HTTPTransport.hpp](../../include/mcp/HTTPTransport.hpp)

Factory config is a `;`-separated `key=value` list. Recognized keys: `scheme`, `host`, `port`, `rpcPath`, `notifyPath`, `serverName`, `caFile`, `caPath`, `connectTimeoutMs`, `readTimeoutMs`.

Example:

```cpp
#include "mcp/HTTPTransport.hpp"

mcp::HTTPTransportFactory f;
auto t = f.CreateTransport("scheme=http; host=127.0.0.1; port=9443; rpcPath=/mcp/rpc; notifyPath=/mcp/notify; serverName=127.0.0.1");
```

#### HTTP/HTTPS authentication

Optional authentication for the HTTP client transport is supported.

- Config keys (semicolon‑delimited `key=value`):
  - `auth` — `none` (default) | `bearer` | `oauth2`
  - `bearerToken` or `token` — static bearer token when `auth=bearer`
  - `oauthUrl` or `oauthTokenUrl` — OAuth2 token endpoint URL (e.g., `https://auth.example.com/oauth2/token`)
  - `clientId` — OAuth2 client id (client‑credentials grant)
  - `clientSecret` — OAuth2 client secret (client‑credentials grant)
  - `scope` — optional space‑delimited scopes
  - `tokenRefreshSkewSeconds` or `tokenSkew` — pre‑expiry refresh skew (seconds, default 60)

Examples:

```cpp
// Static Bearer token
auto t1 = f.CreateTransport(
  "scheme=https; host=api.example.com; port=443; rpcPath=/mcp/rpc; notifyPath=/mcp/notify;"
  " auth=bearer; bearerToken=XYZ"
);

// OAuth2 client‑credentials (token cached and refreshed proactively)
auto t2 = f.CreateTransport(
  "scheme=https; host=api.example.com; port=443; rpcPath=/mcp/rpc; notifyPath=/mcp/notify;"
  " auth=oauth2; oauthUrl=https://auth.example.com/oauth2/token; clientId=myid; clientSecret=mysecret; scope=a b c; tokenSkew=60"
);
```

Notes:

- HTTPS uses TLS 1.3 and hostname verification (SNI) by default, sharing the same trust configuration as normal requests.
- Secrets are not logged. Prefer environment variables or secure config handling in your process to populate `clientSecret`.
- The token endpoint response must include `access_token` and may include `expires_in` (seconds). When absent, a default 1‑hour lifetime is assumed.

#### HTTP client diagnostics and debug logging

The HTTP client surfaces transport‑level diagnostics via `HTTPTransport::SetErrorHandler()`. When an error handler is registered, the transport emits concise stage markers to help troubleshoot request lifecycles and shutdown behavior.

Examples of messages you may see when debug is enabled:

- `HTTP DEBUG: resolved <host>:<port> path=<path>`
- `HTTP DEBUG: http connected` / `https connected`
- `HTTP DEBUG: http wrote request` / `https wrote request`
- `HTTP DEBUG: http read response bytes=<N>` (or `https ...`)
- `HTTPTransport: coPostJson done; body.size=<N>; key=<id>`
- `HTTPTransport: parsed response id=<id|null>`
- `HTTPTransport: deliver lookup key=<id> hit|miss; pending=<count>`
- `HTTPTransport: set_value start` / `HTTPTransport: set_value done`
- `HTTP Close: begin / ioc.stop() called / joining ioThread / ioThread joined / failing pending size=<count>`

Quick usage with shorter timeouts for fast failure in tests:

```cpp
#include "mcp/HTTPTransport.hpp"

mcp::HTTPTransportFactory f;
auto t = f.CreateTransport(
  "scheme=http; host=127.0.0.1; port=8080; rpcPath=/rpc; notifyPath=/notify; "
  "connectTimeoutMs=500; readTimeoutMs=1500; auth=bearer; bearerToken=XYZ"
);

t->SetErrorHandler([](const std::string& msg){
  std::cerr << "[HTTPTransport] " << msg << std::endl;
});

(void)t->Start().get();
// ... SendRequest / SendNotification ...
(void)t->Close().get();
```

Notes:

- Debug messages are emitted only when an error handler is set. They are intended for diagnostics and may evolve; do not assert on their exact strings in production.
- `connectTimeoutMs` controls the connect phase deadline; `readTimeoutMs` bounds the write+read phase per request.

## ITransportAcceptor (server-side acceptors)

Header: [include/mcp/Transport.h](../../include/mcp/Transport.h) (ITransportAcceptor) and [include/mcp/HTTPServer.hpp](../../include/mcp/HTTPServer.hpp)

Use an acceptor to run a server that receives JSON-RPC over HTTP/HTTPS. The acceptor invokes server-provided handlers directly.

### HTTPServerFactory and configuration

Header: [include/mcp/HTTPServer.hpp](../../include/mcp/HTTPServer.hpp)

- Config is a URL-like string with optional query parameters:
  - `http://<host>[:port]`
  - `https://<host>[:port]?cert=<path>&key=<path>`
  - Default port: 9443

Example server wiring using `Server::HandleJSONRPC(...)` bridge:

```cpp
#include "mcp/HTTPServer.hpp"
#include "mcp/Server.h"

mcp::ServerFactory sf;
auto server = sf.CreateServer({"Demo","1.0.0"});

mcp::HTTPServerFactory hf;
auto acceptor = hf.CreateTransportAcceptor("http://127.0.0.1:9443");
acceptor->SetRequestHandler([&server](const mcp::JSONRPCRequest& req){
  return server->HandleJSONRPC(req);
});
acceptor->SetNotificationHandler([](std::unique_ptr<mcp::JSONRPCNotification> note){ /* optional */ });
acceptor->SetErrorHandler([](const std::string& err){ /* log */ });
acceptor->Start().get();
```
### Environment variables

- `MCP_STDIOTRANSPORT_TIMEOUT_MS` — default request timeout in milliseconds. See usage in [src/mcp/StdioTransport.cpp](../../src/mcp/StdioTransport.cpp).
- `MCP_STDIO_CONFIG` — demo/server env to pass the same `key=value` list as the factory config. See examples in [BUILD+TEST.MD](../../BUILD+TEST.MD#demo-and-transport-options-env--factory-config).

## Demo CLI for transport selection

The demo client and server now support selecting transports via command-line arguments.

- Server: [examples/mcp_server/main.cpp](../../examples/mcp_server/main.cpp)
- Client: [examples/mcp_client/main.cpp](../../examples/mcp_client/main.cpp)

Usage examples:

```bash
# stdio (default)
./mcp_server --transport=stdio --stdiocfg="timeout_ms=30000"
./mcp_client --transport=stdio --stdiocfg="timeout_ms=30000"

# shared memory (creator/server uses create=true)
./mcp_server --transport=shm --channel=mcp-shm
./mcp_client --transport=shm --channel=mcp-shm

# http (single request per connection; demo acceptor)
./mcp_server --transport=http --listen=http://127.0.0.1:9443
./mcp_client --transport=http --url=http://127.0.0.1:9443
```

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
