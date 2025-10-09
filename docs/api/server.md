<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: docs/api/server.md
Purpose: Server API reference (IServer / Server)
==========================================================================================================
-->
# Server API (IServer / Server)

Header: [include/mcp/Server.h](../../include/mcp/Server.h)

This page summarizes the public server APIs with signatures and brief descriptions.

## Experimental capabilities

The MCP specification allows optional, non-standard extensions to be advertised during the initialize handshake under `capabilities.experimental`. These keys are:

- Optional and may change over time.
- Safe to ignore by peers that do not recognize them.
- A way to pilot features without changing core protocol messages.

In this SDK the server and/or client use the experimental map for:

- `capabilities.experimental.keepalive` (server → client) — see [Keepalive / Heartbeat](#keepalive--heartbeat).
- `capabilities.experimental.loggingRateLimit` (server → client) — see [Logging rate limiting (experimental)](#logging-rate-limiting-experimental).
- `capabilities.experimental.logLevel` (client → server) — see [Logging to client](#logging-to-client) for client-selected minimum log level.
- `capabilities.experimental.resourceReadChunking` (server → client) — see [Resource read chunking (experimental)](#resource-read-chunking-experimental).

## Type aliases and handlers
- using ToolResult = CallToolResult
- using ResourceContent = ReadResourceResult
- using PromptResult = GetPromptResult
- using ToolHandler = std::function<std::future<ToolResult>(const JSONValue&, std::stop_token)>
- using ResourceHandler = std::function<std::future<ResourceContent>(const std::string&, std::stop_token)>
- using PromptHandler = std::function<PromptResult(const JSONValue&)>
- using SamplingHandler = std::function<std::future<JSONValue>(
    const JSONValue& messages,
    const JSONValue& modelPreferences,
    const JSONValue& systemPrompt,
    const JSONValue& includeContext)>

## Cancellation with std::stop_token

Handlers are asynchronous and cancellable. The server propagates cancellation by requesting stop on a per-request `std::stop_source`. Your handler receives a `std::stop_token` and should cooperatively stop work when cancellation is requested.

Recommended patterns:

```cpp
// Tool handler example
server.RegisterTool("echo", [](const JSONValue& args, std::stop_token st) {
  return std::async(std::launch::async, [args, st]() mutable {
    // Periodically check for cancellation
    if (st.stop_requested()) {
      // Perform any cleanup and return early
      ToolResult r; r.isError = true; return r;
    }
    ToolResult r; r.isError = false; /* populate r.content */
    return r;
  });
});

// Resource handler example
server.RegisterResource("mem://x", [](const std::string& uri, std::stop_token st) {
  return std::async(std::launch::async, [uri, st]() {
    // Optionally use std::stop_callback to react to cancellation
    std::stop_callback onStop(st, [](){ /* cancel I/O, signal worker, etc. */ });
    ReadResourceResult out; /* populate out.contents */
    return out;
  });
});
```

Notes:

- The server will also check for cancellation before and after invoking your handler and return an error shaped as `{"code": -32603, "message": "Cancelled"}` if applicable.
- For long operations, check `st.stop_requested()` periodically or wire cancellation into your I/O primitives where supported.

## Lifecycle / initialize
- std::future<void> Start(std::unique_ptr<ITransport> transport)
- std::future<void> Stop()
- bool IsRunning() const
- std::future<void> HandleInitialize(const Implementation& clientInfo,
                                    const ClientCapabilities& capabilities)

Initialize semantics:

- The server handles the JSON-RPC `initialize` request internally (see `Server::Impl::coStart()` in [src/mcp/Server.cpp](../../src/mcp/Server.cpp)).
- After responding to `initialize`, the server sends:
  - `notifications/initialized`
  - Exactly one each of: `notifications/tools/list_changed`, `notifications/resources/list_changed`, `notifications/prompts/list_changed` (so clients can discover current state immediately).
- The public `Server::HandleInitialize(...)` helper does not send list-changed notifications. It only records client capabilities for non-JSON-RPC wiring scenarios. In normal JSON-RPC flows you do not need to call it.
- Tests assert exactly one list-changed notification per category after initialize. See [tests/test_initialize_notifications.cpp](../../tests/test_initialize_notifications.cpp).

### Bridging requests from an acceptor (HTTPServer)

When running a server behind an `ITransportAcceptor` (e.g., `HTTPServer`), you can bridge incoming JSON-RPC requests directly into the server's dispatcher using `Server::HandleJSONRPC(...)`:

```cpp
#include "mcp/Server.h"
#include "mcp/HTTPServer.hpp"

mcp::ServerFactory sf;
auto server = sf.CreateServer({"Demo","1.0.0"});

mcp::HTTPServerFactory hf;
auto acceptor = hf.CreateTransportAcceptor("http://127.0.0.1:9443");
acceptor->SetRequestHandler([&server](const mcp::JSONRPCRequest& req){
  return server->HandleJSONRPC(req);
});
acceptor->Start().get();
```

Notes:

- The server will only emit `notifications/initialized` and list-changed notifications when it owns a connected `ITransport`. In the acceptor-bridged mode, notifications are not sent automatically; callers should coordinate any desired notifications at a higher level.

### Capability advertisement

- The server advertises formal capabilities in the initialize result under the `capabilities` object. In addition to `tools`, `resources`, `prompts`, and `sampling`, the server now also advertises a `logging` capability to indicate support for `notifications/log`.
- Source: [src/mcp/Server.cpp](../../src/mcp/Server.cpp) in `serializeServerCapabilities()` and constructor defaults.
- Type: see `LoggingCapability` and `ServerCapabilities.logging` in [include/mcp/Protocol.h](../../include/mcp/Protocol.h).
- Client parsing: handled in [src/mcp/Client.cpp](../../src/mcp/Client.cpp) `parseServerCapabilities()`.
- Tests: [tests/test_capabilities_logging.cpp](../../tests/test_capabilities_logging.cpp).

## Tools
- void RegisterTool(const std::string& name, ToolHandler handler)
- void RegisterTool(const Tool& tool, ToolHandler handler)
- void UnregisterTool(const std::string& name)
- std::vector<Tool> ListTools()
- std::future<JSONValue> CallTool(const std::string& name, const JSONValue& arguments)

## Resources
- void RegisterResource(const std::string& uri, ResourceHandler handler)
- void UnregisterResource(const std::string& uri)
- std::vector<Resource> ListResources()
- std::future<JSONValue> ReadResource(const std::string& uri)

## Resource read chunking (experimental)

- The server supports optional byte-range parameters for resource reads. When the client includes `offset` and/or `length` in the `resources/read` request, the server will slice returned text content accordingly before responding. The overall result shape remains identical to a normal read.

Advertisement (experimental):

- The initialize response can include `capabilities.experimental.resourceReadChunking` with:
  - `enabled: true`
  - `maxChunkBytes: number` (hard clamp per slice)

Notes and behavior:

- Range slicing applies to text content only. If the resource handler returns non-text content while a range is requested, the server returns an error (InternalError) indicating that chunking requires text content.
- When `offset` is beyond the end, the server returns an empty `contents` array.
- When only `offset` is provided, the slice runs to the end.
- When only `length` is provided, it is interpreted from `offset = 0`.
- When `maxChunkBytes` is advertised, the server will clamp any ranged read to at most this many bytes for each returned slice. If the requested `length` exceeds `maxChunkBytes`, only `maxChunkBytes` bytes are returned. If `length` is not provided, the server still clamps each slice to `maxChunkBytes`.

References:

- Server request path and slicing logic are implemented in `handleResourcesRead` within [src/mcp/Server.cpp](../../src/mcp/Server.cpp).
- Protocol types: `ReadResourceParams` and `ReadResourceResult` in [include/mcp/Protocol.h](../../include/mcp/Protocol.h).

Examples:

```cpp
// Client (typed wrappers)
using namespace mcp;
// Slice 4 bytes starting at offset 3
auto rr = typed::readResourceRange(*client, "mem://doc", std::optional<int64_t>(3), std::optional<int64_t>(4)).get();
for (const auto& s : typed::collectText(rr)) {
  std::cout << s; // prints the slice
}

// Read an entire resource in fixed-size chunks and reassemble
auto agg = typed::readAllResourceInChunks(*client, "mem://doc", /*chunkSize=*/8192).get();
std::string all;
for (const auto& s : typed::collectText(agg)) all += s;
```


### Configuration API

- `void SetResourceReadChunkingMaxBytes(const std::optional<size_t>& maxBytes)`
  - Configures a hard clamp for ranged reads per returned slice.
  - When set to a positive value, the server clamps slices to at most `maxBytes` bytes and advertises this in `capabilities.experimental.resourceReadChunking.maxChunkBytes` on the next initialize.
  - When unset or set to `0`, no clamp is enforced (the server still advertises `enabled: true`).
  - Note: Changing this value after the initialize handshake affects enforcement immediately for subsequent reads, but does not retroactively update what the client already observed in the initial capabilities advertisement.

Tests:

- Positive and boundary cases are covered in [tests/test_resource_read_chunking.cpp](../../tests/test_resource_read_chunking.cpp).
- Capability absence fallback (range works even if not advertised) is covered in [tests/test_resource_read_chunking_capability_absence.cpp](../../tests/test_resource_read_chunking_capability_absence.cpp).

## Resource templates
- void RegisterResourceTemplate(const ResourceTemplate& resourceTemplate)
- void UnregisterResourceTemplate(const std::string& uriTemplate)
- std::vector<ResourceTemplate> ListResourceTemplates()

## Prompts
- void RegisterPrompt(const std::string& name, PromptHandler handler)
- void UnregisterPrompt(const std::string& name)
- std::vector<Prompt> ListPrompts()
- std::future<JSONValue> GetPrompt(const std::string& name, const JSONValue& arguments)

- void SetSamplingHandler(SamplingHandler handler)
  - Register a server-side handler for `sampling/createMessage`.
- std::future<JSONValue> RequestCreateMessage(const CreateMessageParams& params)
  - Request the client to create a message (server-initiated sampling path).
 - std::future<JSONValue> RequestCreateMessageWithId(const CreateMessageParams& params, const std::string& requestId)
  - Same as above, but allows the server to specify the JSON-RPC request id. This is useful for end-to-end cancellation tests or workflows where the server needs to target a specific in-flight request with a `notifications/cancelled` message.

Cancellation (server-initiated):

- The server can request cancellation of an in-flight client request by sending:

```cpp
JSONValue::Object cancelParams; cancelParams["id"] = std::make_shared<JSONValue>(std::string("<request-id>"));
server.SendNotification(Methods::Cancelled, JSONValue{cancelParams}).get();
```

- When the client observes this notification, it will propagate `std::stop_token` to a cancelable sampling handler (if registered) and ultimately respond with an error shaped as `{ code: -32603, message: "Cancelled" }`.

Tests: see `tests/test_sampling_cancellation_e2e.cpp`.

 ## Keepalive / Heartbeat
 - void SetKeepaliveIntervalMs(const std::optional<int>& intervalMs)
  - Enable periodic `notifications/keepalive` when > 0; disables when not set or <= 0.
 - void SetKeepaliveFailureThreshold(const std::optional<unsigned int>& threshold)
  - Configure the number of consecutive keepalive send failures before closing the transport (min 1; default 3).

 Details:

 - When enabled, the server advertises an experimental capability in initialize response under `capabilities.experimental.keepalive` with fields:
  - `enabled: true`
  - `intervalMs: number` (the configured interval)
  - `failureThreshold: number` (consecutive send failures before transport is closed)
- If keepalive is disabled (`intervalMs` unset or <= 0), the advertisement is removed.
- Consecutive failures to send keepalive notifications are tracked; once the threshold is reached the server will close the transport and invoke the error handler.
- The threshold can be updated at runtime via `SetKeepaliveFailureThreshold(...)`. When keepalive is enabled, the advertisement is refreshed to reflect the new threshold.

 Example:

 ```cpp
 // Enable 100ms cadence and set custom failure threshold
 server.SetKeepaliveIntervalMs(100);
 server.SetKeepaliveFailureThreshold(5);

 // Disable later
 server.SetKeepaliveIntervalMs(std::optional<int>(0));
 ```

For a runnable example that prints keepalive notifications and simulates failures, see `examples/keepalive_demo`.

## Logging to client
- std::future<void> LogToClient(const std::string& level, const std::string& message,
  - Sends `notifications/log` with level/message/data; suppressed if below client-advertised log level (`capabilities.experimental.logLevel`).

Details:

- A client may advertise a minimum log level via `capabilities.experimental.logLevel` during initialize. The server will suppress log notifications below this threshold.
- Supported levels include `DEBUG`, `INFO`, `WARN`/`WARNING` (aliases), `ERROR`, `FATAL`. Unknown levels map conservatively and may be suppressed when the client minimum is higher.
- If the `data` parameter is not provided, the `data` field is omitted in the notification payload.

Notes:

- The server formally advertises logging support via `capabilities.logging = {}` in its initialize response so clients can gate logging features based on presence.

### Logging rate limiting (experimental)

- void SetLoggingRateLimitPerSecond(const std::optional<unsigned int>& perSecond)
  - Configures a simple per-second throttle for `notifications/log`. When set to a non-zero value, at most `perSecond` log notifications will be delivered per second. When not set or `0`, throttling is disabled (subject only to the client's minimum log level).

Advertisement (experimental):

- When enabled, the initialize response includes `capabilities.experimental.loggingRateLimit` with:
  - `enabled: true`
  - `perSecond: number`
- When disabled (unset or `0`), the advertisement is removed.

Notes:

- Throttling is applied after the client-min-level filter and before the notification is sent.
- See tests in [tests/test_logging_rate_limit.cpp](../../tests/test_logging_rate_limit.cpp).

Example:

```cpp
// Client sets experimental.logLevel = "WARN". The server will suppress INFO/DEBUG.
server.LogToClient("INFO", "this-will-be-suppressed", std::nullopt);
server.LogToClient("ERROR", "this-will-be-delivered", std::nullopt);

// Structured payload
JSONValue::Object obj; obj["op"] = std::make_shared<JSONValue>(std::string("index"));
server.LogToClient("WARN", "slow-path", JSONValue{obj}).get();
```

## Notifications and messaging
- std::future<void> NotifyResourcesListChanged()
- std::future<void> NotifyResourceUpdated(const std::string& uri)
- std::future<void> NotifyToolsListChanged()
- std::future<void> NotifyPromptsListChanged()
- std::future<void> SendNotification(const std::string& method, const JSONValue& params)
- std::future<void> SendProgress(const std::string& token, double progress, const std::string& message)

## Error handling and capabilities
- using ErrorHandler = std::function<void(const std::string& error)>;
- void SetErrorHandler(ErrorHandler handler)
- ServerCapabilities GetCapabilities() const
- void SetCapabilities(const ServerCapabilities& capabilities)

## Validation (opt-in)

The server exposes a validation mode toggle (Off/Strict) to enable upcoming runtime shape checks for request/response payloads. See details and usage in [docs/api/validation.md](./validation.md).
