# MCP C++ SDK Comparison: vparla/mcp-cpp vs. cpp-mcp (plan)

This document compares the current MCP C++ SDK implementation (vparla/mcp-cpp) to the other C++ MCP SDK plan at:
- Other SDK plan: [c:\Work\mcp-go\cpp-mcp\cpp-mcp\plan.md](c:\Work\mcp-go\cpp-mcp\cpp-mcp\plan.md)

Key references in this SDK:
- Server implementation: [c:\Work\mcp-cpp\src\mcp\Server.cpp](c:\Work\mcp-cpp\src\mcp\Server.cpp)
- Client implementation: [c:\Work\mcp-cpp\src\mcp\Client.cpp](c:\Work\mcp-cpp\src\mcp\Client.cpp)
- Protocol types: [c:\Work\mcp-cpp\include\mcp\Protocol.h](c:\Work\mcp-cpp\include\mcp\Protocol.h)
- Server API docs: [c:\Work\mcp-cpp\docs\api\server.md](c:\Work\mcp-cpp\docs\api\server.md)
- Parity matrix (vs spec): [c:\Work\mcp-cpp\docs\parity-matrix.md](c:\Work\mcp-cpp\docs\parity-matrix.md)

Representative tests proving capability coverage in this SDK:
- Keepalive: [c:\Work\mcp-cpp\tests\test_keepalive.cpp](c:\Work\mcp-cpp\tests\test_keepalive.cpp)
- Logging: [c:\Work\mcp-cpp\tests\test_server_logging.cpp](c:\Work\mcp-cpp\tests\test_server_logging.cpp), [c:\Work\mcp-cpp\tests\test_capabilities_logging.cpp](c:\Work\mcp-cpp\tests\test_capabilities_logging.cpp)
- Server-initiated sampling: [c:\Work\mcp-cpp\tests\test_server_initiated_sampling.cpp](c:\Work\mcp-cpp\tests\test_server_initiated_sampling.cpp)
- Cancellation: [c:\Work\mcp-cpp\tests\test_cancellation.cpp](c:\Work\mcp-cpp\tests\test_cancellation.cpp)
- Progress: [c:\Work\mcp-cpp\tests\test_progress.cpp](c:\Work\mcp-cpp\tests\test_progress.cpp)
- Initialize notifications: [c:\Work\mcp-cpp\tests\test_initialize_notifications.cpp](c:\Work\mcp-cpp\tests\test_initialize_notifications.cpp)
- Paging and metadata: [c:\Work\mcp-cpp\tests\test_client_paging.cpp](c:\Work\mcp-cpp\tests\test_client_paging.cpp), [c:\Work\mcp-cpp\tests\test_tools_inputschema.cpp](c:\Work\mcp-cpp\tests\test_tools_inputschema.cpp)

## Summary

- The vparla/mcp-cpp SDK has complete JSON-RPC MCP coverage for core features, with strong tests and Docker-first cross-platform CI.
- The other C++ SDK plan emphasizes an HTTP/SSE transport with streaming, authentication, reconnection/backoff, and session resumption; in this SDK the HTTP client transport (with Bearer and OAuth 2.1 client-credentials) is implemented, while SSE/streaming and reconnection/backoff remain out-of-scope.
- Newly added: formal `capabilities.logging` advertisement (mirrors Go SDK) is implemented and tested here.

## Feature-by-Feature Comparison

- Tools
  - vparla/mcp-cpp: Implemented list (paged), metadata with input schemas, invocation, list-changed notification. See [Server.cpp](c:\Work\mcp-cpp\src\mcp\Server.cpp) and tests above.
  - cpp-mcp (plan): Targeting full client/server support; includes typed wrappers and streaming tool execution as stretch goals.
  - Gap: Typed wrappers and streaming execution (plan specific) are not implemented in vparla/mcp-cpp.

- Resources
  - vparla/mcp-cpp: Implemented list (paged), read, templates (register/list), subscriptions (global and per-URI), list/updated notifications. See tests mentioned.
  - cpp-mcp (plan): Resource subscription and content streaming for large resources planned.
  - Gap: Streaming resource content not implemented here.

- Prompts
  - vparla/mcp-cpp: Implemented list (paged) and get prompt.
  - cpp-mcp (plan): Typed results and dynamic generation planned.
  - Gap: Typed prompt results and dynamic generation not implemented here.

- Sampling (server-initiated)
  - vparla/mcp-cpp: Implemented `sampling/createMessage` with client-registered handler. Tested end-to-end.
  - cpp-mcp (plan): Included in session features.
  - Gap: None for core MCP shape.

- Cancellation
  - vparla/mcp-cpp: Implemented via `std::stop_token` propagation; supports cancellations and error shaping in both tools and resources.
  - cpp-mcp (plan): Planned.
  - Gap: None for core MCP shape.

- Progress
  - vparla/mcp-cpp: Implemented progress notifications and client handler.
  - cpp-mcp (plan): Planned.
  - Gap: None.

- Keepalive / Heartbeat
  - vparla/mcp-cpp: Implemented periodic `notifications/keepalive`, failure threshold with transport close, experimental capability advertisement, tests.
  - cpp-mcp (plan): Not explicitly called out, but reconnection and flow control are planned.
  - Gap: None on keepalive; reconnection/backoff is out-of-scope here.

- Logging to Client
  - vparla/mcp-cpp: Implemented `notifications/log` respecting client `experimental.logLevel`, and now formally advertises `capabilities.logging`. See [Server.cpp](c:\Work\mcp-cpp\src\mcp\Server.cpp), [Client.cpp](c:\Work\mcp-cpp\src\mcp\Client.cpp), and tests referenced above.
  - cpp-mcp (plan): Enhanced logging, rate limiting planned.
  - Gap: Rate limiting not implemented here.

- Transports
  - vparla/mcp-cpp: InMemory, Stdio, SharedMemory, and HTTP client transports implemented. HTTP client supports Bearer and OAuth 2.1 client-credentials auth; integration demo test covers HTTP. See [Authentication](./api/auth.md) for flows and demos.
  - cpp-mcp (plan): HTTP/SSE transport with streaming, auth (OAuth/API keys/custom headers), compression, proxy support, reconnection/backoff.
  - Gap (intentional): SSE/streaming, compression, proxy support, and reconnection/backoff are not in current scope here.

- Session
  - vparla/mcp-cpp: Unified initialize path (exactly one set of list-changed notifications), strong tests. No caching/resumption logic.
  - cpp-mcp (plan): Graceful lifecycle/resumption, caching of listings, auto-reconnect, rate limiting/flow control, schema validation.
  - Gaps: Caching, auto-reconnect, session resumption, rate limiting/flow control, schema validation.

- Testing & CI
  - vparla/mcp-cpp: Comprehensive GoogleTests including negative tests; Docker-first CI on Ubuntu/macOS/Windows plus Windows native smoke. See [c:\Work\mcp-cpp\BUILD+TEST.MD](c:\Work\mcp-cpp\BUILD+TEST.MD).
  - cpp-mcp (plan): Comprehensive tests and benchmarks planned.
  - Gap: Performance benchmarks (planned in other SDK) not present here.

## Newly Added Parity Item

- Logging Capability Advertisement
  - Implemented `LoggingCapability` and added `ServerCapabilities.logging` in [Protocol.h](c:\Work\mcp-cpp\include\mcp\Protocol.h).
  - Server advertises `capabilities.logging` in initialize via `serializeServerCapabilities()` and default initialization in [Server.cpp](c:\Work\mcp-cpp\src\mcp\Server.cpp).
  - Client parses `capabilities.logging` in [Client.cpp](c:\Work\mcp-cpp\src\mcp\Client.cpp).
  - Tests: [test_capabilities_logging.cpp](c:\Work\mcp-cpp\tests\test_capabilities_logging.cpp).

## Gaps and Recommendations

- Out-of-scope (transport layer): SSE/streaming, compression, proxy, reconnection/backoff. (HTTP client with auth is implemented.)
- Session enhancements: session resumption, listing caches, auto-reconnect, rate limiting/flow control.
- Schema validation: optional runtime validation of server responses and inputs.
- Streaming: streaming tool execution and resource content for large payloads.
- Typed wrappers: convenience typed client wrappers for tools/resources/prompts.
- Performance: throughput/latency benchmarks and stress tests.
- Logging: optional rate limiting to prevent log flood.

Suggested prioritization (if pursuing parity with the other SDK plan):
1. Schema validation and typed wrappers (low risk, high developer ergonomics).
2. Optional logging rate limiting.
3. Performance benchmarks and stress tests.
4. Session conveniences (caching) and auto-reconnect (if a future HTTP transport is added).
5. HTTP/SSE transport and streaming (separate project phase).

## Appendix: Plan Highlights from the Other SDK

From [plan.md](c:\Work\mcp-go\cpp-mcp\cpp-mcp\plan.md):
- HTTP/SSE transport completion with streaming and auth.
- Session lifecycle/resumption, logging, rate limiting, flow control.
- Client caching, resource subscriptions, auto-reconnect, progress reporting.
- Tool/resource/prompt registrations, typed wrappers, streaming execution/content.
- Comprehensive tests and performance benchmarks.
