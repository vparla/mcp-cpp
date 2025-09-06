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

## Resource templates
- void RegisterResourceTemplate(const ResourceTemplate& resourceTemplate)
- void UnregisterResourceTemplate(const std::string& uriTemplate)
- std::vector<ResourceTemplate> ListResourceTemplates()

## Prompts
- void RegisterPrompt(const std::string& name, PromptHandler handler)
- void UnregisterPrompt(const std::string& name)
- std::vector<Prompt> ListPrompts()
- std::future<JSONValue> GetPrompt(const std::string& name, const JSONValue& arguments)

## Sampling
- void SetSamplingHandler(SamplingHandler handler)
  - Register a server-side handler for `sampling/createMessage`.
- std::future<JSONValue> RequestCreateMessage(const CreateMessageParams& params)
  - Request the client to create a message (server-initiated sampling path).

## Keepalive / Heartbeat
- void SetKeepaliveIntervalMs(const std::optional<int>& intervalMs)
  - Enable periodic `notifications/keepalive` when > 0; disables when not set or <= 0.

Details:

- When enabled, the server advertises an experimental capability in initialize response under `capabilities.experimental.keepalive` with fields:
  - `enabled: true`
  - `intervalMs: number` (the configured interval)
  - `failureThreshold: number` (consecutive send failures before transport is closed)
- If keepalive is disabled (`intervalMs` unset or <= 0), the advertisement is removed.
- Consecutive failures to send keepalive notifications are tracked; once the threshold is reached the server will close the transport and invoke the error handler.

Example:

```cpp
// Enable 100ms cadence
server.SetKeepaliveIntervalMs(100);

// Disable later
server.SetKeepaliveIntervalMs(std::optional<int>(0));
```

## Logging to client
- std::future<void> LogToClient(const std::string& level, const std::string& message,
                               const std::optional<JSONValue>& data = std::nullopt)
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
