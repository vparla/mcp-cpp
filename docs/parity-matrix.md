//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: docs/parity-matrix.md
// Purpose: Feature parity matrix for the MCP C++ SDK vs. MCP spec
//==========================================================================================================

# Parity Matrix

This matrix tracks the MCP C++ SDK feature coverage relative to the MCP spec.

- Tools
  - List tools (paged): Implemented
  - Call tool (server → client): Implemented
  - Input schema (tool metadata): Implemented (tests: `ToolsInputSchema.*` in `tests/test_tools_inputschema.cpp`)
  - Icon and title metadata: Implemented (`tests/test_icon_parity.cpp`)
  - List-changed notifications: Implemented (one-time on initialize)

- Resources
  - List resources (paged): Implemented
  - Resource metadata (`title`, `size`, `annotations`, `_meta`, `icons`): Implemented (`tests/test_icon_parity.cpp`)
  - Read resource (server → client): Implemented (shape covered by tests)
  - Resource templates (list, register/unregister): Implemented (`tests/test_resource_templates.cpp`)
  - Resource template metadata (`title`, `annotations`, `_meta`, `icons`): Implemented (`tests/test_icon_parity.cpp`)
  - Subscriptions (global and per-URI): Implemented (`tests/test_resource_subscriptions.cpp`)
  - List/updated notifications: Implemented
  - Read chunking (experimental offset/length): Implemented (`tests/test_resource_read_chunking.cpp`, `tests/test_resource_read_chunking_capability_absence.cpp`)
  - Listings cache: Implemented with TTL and invalidation (`tests/test_client_cache_extended.cpp`)

- Prompts
  - List prompts (paged): Implemented
  - Prompt metadata (`title`, `arguments`, `_meta`, `icons`): Implemented (`tests/test_icon_parity.cpp`)
  - Get prompt: Implemented (returns `PromptResult.messages`)
  - List-changed notifications: Implemented

- Utilities
  - `ping`: Implemented
  - `completion/complete`: Implemented
  - `roots/list` and `notifications/roots/list_changed`: Implemented
  - `elicitation/create`: Implemented
  - `tasks/get`, `tasks/list`, `tasks/result`, and `tasks/cancel`: Implemented (`tests/test_tasks.cpp`)

- Sampling
  - Client-registered sampling handler: Implemented (client API)
  - Server-initiated `sampling/createMessage`: Implemented (`tests/test_server_initiated_sampling.cpp`)
  - Capability advertisement: Implemented (only when handler set)
  - Server-initiated cancellation (E2E) via `notifications/cancelled`: Implemented (`tests/test_sampling_cancellation_e2e.cpp`)

- Tasks
  - Task capability negotiation: Implemented for server tool tasks and client sampling/elicitation tasks
  - Task-augmented `tools/call`: Implemented
  - Task-augmented `sampling/createMessage` and `elicitation/create`: Implemented
  - `notifications/tasks/status`: Implemented

- Cancellation
  - Cancellation propagation to long-running handlers: Implemented (`tests/test_cancellation.cpp`)
  - `notifications/cancelled` handling: Implemented

- Notifications & Progress
  - Arbitrary server notifications: Implemented
  - Progress notifications: Implemented (`tests/test_progress.cpp`)

- Keepalive / Heartbeat
  - Periodic `notifications/keepalive`: Implemented (`tests/test_keepalive.cpp`)
  - Failure threshold → close transport (configurable): Implemented (`tests/test_keepalive_threshold_config.cpp`)
  - Capability advertisement (`capabilities.experimental.keepalive`): Implemented

- Logging to client
  - `notifications/log` with level/message/data: Implemented (`tests/test_server_logging.cpp`)
  - Client min log level (`capabilities.experimental.logLevel`): Implemented
  - Capability advertisement (`capabilities.logging`): Implemented (`tests/test_capabilities_logging.cpp`)

- Transports
  - InMemory: Implemented (unit-test transport)
  - Stdio: Implemented (demo integration test in CTest)
  - HTTP streamable transport/session lifecycle: Implemented (`tests/test_http_streamable.cpp`)
  - Stdio hardening negative tests: Implemented (idle read timeout, write queue overflow, write timeout, bad Content-Length)

- CI
  - Docker-first tests (Ubuntu): Implemented (GitHub Actions)
  - Docker-first tests (macOS): Implemented
  - Docker-first tests (Windows): Implemented

Notes:
- See `docs/api/server.md` for keepalive and logging details.
- See `BUILD+TEST.MD` for Docker-first workflows on all platforms.

---

## Detailed Parity by Spec Area (MCP 2025-11-25)

- **[Protocol Version]**
  - Version constant: `PROTOCOL_VERSION = "2025-11-25"`.
  - Source: [include/mcp/Protocol.h](../include/mcp/Protocol.h)

- **[Initialize Handshake & Post-Init Notifications]**
  - Behavior: server handles `initialize`, then emits `notifications/initialized` and exactly one `.../list_changed` each for tools/resources/prompts.
  - Source: [src/mcp/Server.cpp](../src/mcp/Server.cpp), docs at [docs/api/server.md](./api/server.md#lifecycle--initialize)
  - Tests: [tests/test_initialize_notifications.cpp](../tests/test_initialize_notifications.cpp)

- **[Capabilities Advertisement]**
  - `ServerCapabilities` includes `tools`, `resources`, `prompts`, `sampling`, `completions`, `logging`, and `experimental` map.
  - `ClientCapabilities` includes `roots`, `sampling`, `elicitation`, `experimental`, and `extensions`.
  - Source: [include/mcp/Protocol.h](../include/mcp/Protocol.h)
  - Logging and completion capabilities are explicitly advertised; roots and elicitation are negotiated from the client.
  - Tests: [tests/test_capabilities_logging.cpp](../tests/test_capabilities_logging.cpp)

- **[Tools]**
  - List (paged): Implemented.
  - Call: Implemented.
  - Input schema metadata: Implemented.
  - Title/icon metadata: Implemented.
  - List-changed notifications: Implemented, single-shot post-init.
  - Sources: [include/mcp/Protocol.h](../include/mcp/Protocol.h), [src/mcp/Server.cpp](../src/mcp/Server.cpp), [src/mcp/Client.cpp](../src/mcp/Client.cpp)
  - Tests: [tests/test_client_paging.cpp](../tests/test_client_paging.cpp), [tests/test_tools_inputschema.cpp](../tests/test_tools_inputschema.cpp)

- **[Resources]**
  - List (paged), Read, Templates (list/register/unregister): Implemented.
  - Resource/resource-template metadata parity for icons and related list metadata: Implemented.
  - Subscriptions: global and per-URI; filtered `notifications/resources/updated`.
  - List/updated notifications: Implemented.
  - Sources: [include/mcp/Protocol.h](../include/mcp/Protocol.h), [src/mcp/Server.cpp](../src/mcp/Server.cpp)
  - Tests: [tests/test_resource_templates.cpp](../tests/test_resource_templates.cpp), [tests/test_resource_subscriptions.cpp](../tests/test_resource_subscriptions.cpp), [tests/test_client_subscribe_uri.cpp](../tests/test_client_subscribe_uri.cpp), [tests/test_read_resource.cpp](../tests/test_read_resource.cpp)

- **[Prompts]**
  - List (paged), Get prompt returns `messages` with correct shape.
  - Title/icon metadata and richer list metadata: Implemented.
  - Sources: [include/mcp/Protocol.h](../include/mcp/Protocol.h)
  - Tests: [tests/test_prompts_get.cpp](../tests/test_prompts_get.cpp), [tests/test_client_paging.cpp](../tests/test_client_paging.cpp), [tests/test_icon_parity.cpp](../tests/test_icon_parity.cpp)

- **[Utilities: Ping, Completion, Roots, Elicitation]**
  - `ping`: Implemented for client->server and server->client flows.
  - `completion/complete`: Implemented with strict validation and typed result parsing.
  - `roots/list` + `notifications/roots/list_changed`: Implemented.
  - `elicitation/create`: Implemented with strict validation and typed request/result parsing.
  - Sources: [include/mcp/Protocol.h](../include/mcp/Protocol.h), [src/mcp/Client.cpp](../src/mcp/Client.cpp), [src/mcp/Server.cpp](../src/mcp/Server.cpp)
  - Tests: [tests/test_completion_ping.cpp](../tests/test_completion_ping.cpp), [tests/test_roots.cpp](../tests/test_roots.cpp), [tests/test_elicitation.cpp](../tests/test_elicitation.cpp)

- **[Tasks]**
  - Task capability negotiation for server-side `tools/call` and client-side `sampling/createMessage` / `elicitation/create`.
  - `tasks/get`, `tasks/list`, `tasks/result`, and `tasks/cancel`: Implemented.
  - `notifications/tasks/status`: Implemented on both requestor sides.
  - Sources: [include/mcp/Protocol.h](../include/mcp/Protocol.h), [src/mcp/Client.cpp](../src/mcp/Client.cpp), [src/mcp/Server.cpp](../src/mcp/Server.cpp)
  - Tests: [tests/test_tasks.cpp](../tests/test_tasks.cpp)

- **[Sampling (Server → Client)]**
  - Server-initiated `sampling/createMessage` with optional cancelable handler on client.
  - Cancellation via `notifications/cancelled` supported end-to-end.
  - Capability advertised when handler present.
  - Sources: [docs/api/client.md](./api/client.md#sampling-server--client), [docs/api/server.md](./api/server.md#prompts), [include/mcp/Protocol.h](../include/mcp/Protocol.h)
  - Tests: [tests/test_server_initiated_sampling.cpp](../tests/test_server_initiated_sampling.cpp), [tests/test_sampling_cancellation_e2e.cpp](../tests/test_sampling_cancellation_e2e.cpp), [tests/test_sampling_handler.cpp](../tests/test_sampling_handler.cpp), [tests/test_sampling_typed_helpers.cpp](../tests/test_sampling_typed_helpers.cpp)

- **[Paging]**
  - `nextCursor` on list results, helpers on client.
  - Sources: [include/mcp/Protocol.h](../include/mcp/Protocol.h), [src/mcp/Client.cpp](../src/mcp/Client.cpp)
  - Tests: [tests/test_client_paging.cpp](../tests/test_client_paging.cpp)

- **[Notifications & Progress]**
  - Implemented: `notifications/initialized`, `notifications/progress`, `notifications/*/list_changed`, `notifications/cancelled`, `notifications/log`, `notifications/resources/updated`.
  - Sources: [include/mcp/Protocol.h](../include/mcp/Protocol.h), [src/mcp/Server.cpp](../src/mcp/Server.cpp), [src/mcp/Client.cpp](../src/mcp/Client.cpp)
  - Tests: [tests/test_progress.cpp](../tests/test_progress.cpp), others listed in sections above.

- **[Error Handling (Structured)]**
  - Typed error structures and mapping for server/client.
  - Source: [include/mcp/errors/Errors.h](../include/mcp/errors/Errors.h)
  - Tests: [tests/test_errors.cpp](../tests/test_errors.cpp), plus negative-path cases throughout the suite.

- **[Keepalive / Heartbeat (Experimental)]**
  - Periodic `notifications/keepalive`, configurable failure threshold closing transport; advertised via `capabilities.experimental.keepalive`.
  - Sources: [docs/api/server.md](./api/server.md#keepalive--heartbeat)
  - Tests: [tests/test_keepalive.cpp](../tests/test_keepalive.cpp), [tests/test_keepalive_threshold_config.cpp](../tests/test_keepalive_threshold_config.cpp)

- **[Logging to Client (+ Rate Limiting Experimental)]**
  - `notifications/log`, client min level via experimental key, formal `capabilities.logging`, optional per-second throttle advertised via `capabilities.experimental.loggingRateLimit`.
  - Sources: [docs/api/server.md](./api/server.md#logging-to-client)
  - Tests: [tests/test_server_logging.cpp](../tests/test_server_logging.cpp), [tests/test_capabilities_logging.cpp](../tests/test_capabilities_logging.cpp), [tests/test_logging_rate_limit.cpp](../tests/test_logging_rate_limit.cpp)

- **[Resource Read Chunking (Experimental)]**
  - `resources/read` supports `offset`/`length`; optional server clamp advertised with `maxChunkBytes`.
  - Sources: [docs/api/server.md](./api/server.md#resource-read-chunking-experimental)
  - Tests: [tests/test_resource_read_chunking.cpp](../tests/test_resource_read_chunking.cpp), [tests/test_resource_read_chunking_capability_absence.cpp](../tests/test_resource_read_chunking_capability_absence.cpp), [tests/test_resource_read_chunking_overload.cpp](../tests/test_resource_read_chunking_overload.cpp)

- **[Validation (Opt‑In)]**
  - Toggle Off/Strict for runtime shape checks; docs describe API.
  - Sources: [docs/api/validation.md](./api/validation.md)
  - Strict validators accept richer non-text content blocks, structured tool outputs, and richer prompt/resource payloads.
  - Tests: [tests/test_content_parity.cpp](../tests/test_content_parity.cpp), [tests/test_validation_strict.cpp](../tests/test_validation_strict.cpp)

- **[Transports]**
  - InMemory: Implemented — [include/mcp/InMemoryTransport.hpp](../include/mcp/InMemoryTransport.hpp)
  - Stdio: Implemented — [include/mcp/StdioTransport.hpp](../include/mcp/StdioTransport.hpp), hardening tests driven by scripts.
  - Shared Memory: Implemented — [include/mcp/SharedMemoryTransport.hpp](../include/mcp/SharedMemoryTransport.hpp)
  - HTTP client: Implemented — [include/mcp/HTTPTransport.hpp](../include/mcp/HTTPTransport.hpp)
  - HTTP server acceptor: Implemented — [include/mcp/HTTPServer.hpp](../include/mcp/HTTPServer.hpp)
  - Streamable HTTP (`POST` endpoint + `GET` SSE stream + session lifecycle): Implemented.
  - Integration demo test: `TransportDemo.Run` — defined in [tests/CMakeLists.txt](../tests/CMakeLists.txt)
  - Streamable HTTP tests: [tests/test_http_streamable.cpp](../tests/test_http_streamable.cpp)

- **[OAuth]**
  - Status: Implemented
    - HTTP client: Bearer and OAuth 2.1 client-credentials.
    - HTTP server acceptor: Bearer authentication middleware with scopes + `WWW-Authenticate` header on failures.
  - Docs: [docs/api/transport.md](./api/transport.md#httphttps-authentication), [docs/api/server.md](./api/server.md#server-side-bearer-authentication-http-acceptor), [docs/api/auth.md](./api/auth.md)
  - Tests: [tests/test_http_auth.cpp](../tests/test_http_auth.cpp), [tests/test_http_server_bearer.cpp](../tests/test_http_server_bearer.cpp)
  - Demo CTests: `HTTPDemo.BearerAuth` and `HTTPDemo.BearerAuth.Forbidden` in [tests/CMakeLists.txt](../tests/CMakeLists.txt)

- **[CI & Build]**
  - Docker-first workflows for Linux/macOS/Windows documented in [BUILD+TEST.MD](../BUILD+TEST.MD); ALL script uses host IPC for SHM paths.

---

If you spot a spec item not reflected here, open an issue or PR to update this matrix and add/adjust tests accordingly.
