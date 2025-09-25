<!-- SPDX-License-Identifier: MIT -->

# MCP C++ SDK

A C++20 SDK for the Model Context Protocol (MCP).

Cross-platform (Windows/macOS/Linux), CMake-based. 
See Docker-first instructions in [BUILD+TEST.MD](./BUILD+TEST.MD).

## Quick start

Docker-first workflow for all platforms. For details, see [BUILD+TEST.MD](./BUILD+TEST.MD).

- Linux/macOS:
```bash
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test .
docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull --load -t mcp-cpp-demo .
docker run --rm --name mcp-cpp-demo --mount type=bind,src=$(pwd),dst=/work mcp-cpp-demo
```

- Windows (PowerShell via WSL2 Ubuntu):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test ."
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull --load -t mcp-cpp-demo ."
wsl -d Ubuntu -- bash -lc "docker run --rm --name mcp-cpp-demo --mount type=bind,src=/mnt/c/Work/mcp-cpp,dst=/work mcp-cpp-demo"
```

## API Reference

This section summarizes the primary SDK interfaces and feature-specific APIs.

- Client API header: [include/mcp/Client.h](c:/Work/mcp-cpp/include/mcp/Client.h)
- Server API header: [include/mcp/Server.h](c:/Work/mcp-cpp/include/mcp/Server.h)
- Transport headers: [include/mcp/Transport.h](c:/Work/mcp-cpp/include/mcp/Transport.h),
  [include/mcp/InMemoryTransport.hpp](c:/Work/mcp-cpp/include/mcp/InMemoryTransport.hpp),
  [include/mcp/StdioTransport.hpp](c:/Work/mcp-cpp/include/mcp/StdioTransport.hpp)

### Client API (IClient / Client)

- Connection: `Connect(transport)`, `Disconnect()`, `IsConnected()`
- Initialize: `Initialize(clientInfo, capabilities)` → `ServerCapabilities`
- Tools: `ListTools()`, `ListToolsPaged(cursor, limit)`, `CallTool(name, args)`
- Resources: `ListResources()`, `ListResourcesPaged(cursor, limit)`, `ReadResource(uri)`
- Resource templates: `ListResourceTemplates()`, `ListResourceTemplatesPaged(cursor, limit)`
- Subscriptions: `SubscribeResources()`, `SubscribeResources(optional<string> uri)`,
  `UnsubscribeResources()`, `UnsubscribeResources(optional<string> uri)`
- Prompts: `ListPrompts()`, `ListPromptsPaged(cursor, limit)`, `GetPrompt(name, args)`
- Sampling (server → client): `SetSamplingHandler(handler)`
- Notifications & progress: `SetNotificationHandler(method, handler)`, `RemoveNotificationHandler(method)`,
  `SetProgressHandler(handler)`, `SetErrorHandler(handler)`

Example (connect + initialize):
```cpp
ClientFactory factory; Implementation info{"MyClient","1.0.0"};
auto client = factory.CreateClient(info);
client->Connect(std::move(transport)).get();
ClientCapabilities caps; auto serverCaps = client->Initialize(info, caps).get();
```

### Server API (IServer / Server)

- Lifecycle: `Start(transport)`, `Stop()`, `IsRunning()`
- Initialize: `HandleInitialize(clientInfo, capabilities)`
- Tools: `RegisterTool(name, handler)` or `RegisterTool(tool, handler)`, `UnregisterTool(name)`, `ListTools()`, `CallTool(name, args)`
- Resources: `RegisterResource(uri, handler)`, `UnregisterResource(uri)`, `ListResources()`, `ReadResource(uri)`
- Resource templates: `RegisterResourceTemplate(t)`, `UnregisterResourceTemplate(template)`, `ListResourceTemplates()`
- Prompts: `RegisterPrompt(name, handler)`, `UnregisterPrompt(name)`, `ListPrompts()`, `GetPrompt(name, args)`
- Sampling: `SetSamplingHandler(handler)`, `RequestCreateMessage(params)`
- Keepalive: `SetKeepaliveIntervalMs(intervalMs)`
- Logging: `LogToClient(level, message, data)`
- Notifications: `NotifyResourcesListChanged()`, `NotifyResourceUpdated(uri)`, `NotifyToolsListChanged()`, `NotifyPromptsListChanged()`
- Messaging: `SendNotification(method, params)`, `SendProgress(token, progress, message)`
- Errors & caps: `SetErrorHandler(handler)`, `GetCapabilities()`, `SetCapabilities(caps)`

### Transport API (ITransport & factories)

- Core: `Start()`, `Close()`, `IsConnected()`, `GetSessionId()`
- Messaging: `SendRequest(req)`, `SendNotification(note)`
- Handlers: `SetNotificationHandler(h)`, `SetErrorHandler(h)`, `SetRequestHandler(h)`
- Implementations:
  - In-memory: [include/mcp/InMemoryTransport.hpp](c:/Work/mcp-cpp/include/mcp/InMemoryTransport.hpp)
  - Stdio: [include/mcp/StdioTransport.hpp](c:/Work/mcp-cpp/include/mcp/StdioTransport.hpp)

### Resource Templates

The SDK supports listing resource templates, allowing clients to discover resource URI patterns exposed by a server.

- Server: register templates using `Server::RegisterResourceTemplate()`.
  Example:

  ```cpp
  mcp::Server server("My MCP Server");
  mcp::ResourceTemplate rt{
      "file:///{path}",
      "File Reader",
      std::optional<std::string>("Reads files"),
      std::optional<std::string>("text/plain")
  };
  server.RegisterResourceTemplate(rt);
  ```

- Client: list templates with `client->ListResourceTemplates()` and inspect fields:

  ```cpp
  auto fut = client->ListResourceTemplates();
  auto templates = fut.get();
  for (const auto& t : templates) {
      // t.uriTemplate, t.name, t.description, t.mimeType
  }
  ```

### Resource Subscriptions

The client supports subscribing to resource update notifications globally or per-URI.

- Global: `client->SubscribeResources()` and `client->UnsubscribeResources()`
- Per-URI: `client->SubscribeResources(std::optional<std::string>("mem://1"))` to subscribe to a single URI. Use `UnsubscribeResources(std::optional<std::string>("mem://1"))` to remove.

Example:

```cpp
// Subscribe to a specific resource URI
auto fut = client->SubscribeResources(std::optional<std::string>("test://a"));
fut.get(); // wait for ack
```

### Paging

The client supports paged listing for tools, resources, resource templates, and prompts. Each List*Paged API
accepts an optional cursor and limit and returns a page plus an optional nextCursor.

Example (tools):

```cpp
// Page 1
auto p1 = client->ListToolsPaged(std::nullopt, 2);
auto r1 = p1.get();
// r1.tools, r1.nextCursor

// Page 2 using nextCursor
auto p2 = client->ListToolsPaged(r1.nextCursor, 2);
auto r2 = p2.get();
```

Similar helpers exist for resources, resource templates, and prompts:

```cpp
auto res = client->ListResourcesPaged(std::nullopt, 10).get();
auto tmpl = client->ListResourceTemplatesPaged(std::nullopt, 10).get();
auto prm = client->ListPromptsPaged(std::nullopt, 10).get();
```

See tests for examples: `ClientPaging.*` in `tests/test_client_paging.cpp`.

### Server-side Sampling

The server can handle client-initiated `sampling/createMessage` by registering a sampling handler.
The handler returns a JSONValue shaped per MCP spec (e.g., model/role/content).

Minimal example:

```cpp
server.SetSamplingHandler([](const JSONValue& messages,
                              const JSONValue& modelPreferences,
                              const JSONValue& systemPrompt,
                              const JSONValue& includeContext) {
   (void)messages; (void)modelPreferences; (void)systemPrompt; (void)includeContext;
   return std::async(std::launch::deferred, [](){
     JSONValue::Object resultObj;
     resultObj["model"] = std::make_shared<JSONValue>(std::string("example-model"));
     resultObj["role"] = std::make_shared<JSONValue>(std::string("assistant"));
     JSONValue::Array contentArr;
     JSONValue::Object textContent;
     textContent["type"] = std::make_shared<JSONValue>(std::string("text"));
     textContent["text"] = std::make_shared<JSONValue>(std::string("hello"));
     contentArr.push_back(std::make_shared<JSONValue>(textContent));
     resultObj["content"] = std::make_shared<JSONValue>(contentArr);
     return JSONValue{resultObj};
   });
});
```

See `ServerSampling.HandlesCreateMessageRequest` in `tests/test_sampling_handler.cpp`.

## Tests

Tests use GoogleTest via CMake FetchContent (build-time only). See [BUILD+TEST.MD](./BUILD+TEST.MD#run-unit-tests-googletest-inside-docker) for the Docker/WSL2 flow.

## Docker Demo

Build and run the client/server demo using stdio transport (WSL2 PowerShell example below):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull --load -t mcp-cpp-demo ."
wsl -d Ubuntu -- bash -lc "docker run --rm --name mcp-cpp-demo --mount type=bind,src=/mnt/c/Work/mcp-cpp,dst=/work mcp-cpp-demo"
```

## Examples

- `examples/typed_quickstart` — minimal end-to-end usage of typed wrappers (tools/resources/prompts).
- `examples/resource_chunking` — demonstrates experimental resource range reads (offset/length) and reassembly of chunks. If the server advertises `capabilities.experimental.resourceReadChunking.maxChunkBytes`, returned slices are hard-clamped to that size; the example shows how to reassemble correctly using typed helpers.

## License

MIT
