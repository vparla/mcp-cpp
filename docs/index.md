<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: docs/index.md
Purpose: Documentation index for the MCP C++ SDK
==========================================================================================================
-->
# MCP C++ SDK Documentation
 
This documentation will cover API reference, installation instructions, and usage examples for the MCP C++ SDK.
 
Contents:
- Getting Started (Docker-first)
- Build & Test: see [BUILD+TEST.MD](../BUILD+TEST.MD)
- API Overview
 - Transports (factory + acceptor + demo CLI): see [docs/api/transport.md](./api/transport.md)
  - HTTP Authentication (Bearer/OAuth 2.1): see [docs/api/transport.md#httphttps-authentication](./api/transport.md#httphttps-authentication)
  - Authentication overview: see [docs/api/auth.md](./api/auth.md)
- Resource Subscriptions (Client API): see [README.md#resource-subscriptions](../README.md#resource-subscriptions)
 - Paging (Client API): see [README.md#paging](../README.md#paging)
 - Server-side Sampling (Server API): see [README.md#server-side-sampling](../README.md#server-side-sampling)
- Keepalive & Logging (Server API): see [docs/api/server.md](./api/server.md#keepalive--heartbeat) and [docs/api/server.md](./api/server.md#logging-to-client)
- Resource Read Chunking (experimental): see [docs/api/server.md](./api/server.md#resource-read-chunking-experimental) and [docs/api/typed.md](./api/typed.md#range-reads-and-chunk-helpers)
- Parity Matrix: see [docs/parity-matrix.md](./parity-matrix.md)
- SDK Comparison: see [docs/cpp-sdk-comparison.md](./cpp-sdk-comparison.md)
- Typed Client Wrappers: see [docs/api/typed.md](./api/typed.md)
- Validation (opt-in): see [docs/api/validation.md](./api/validation.md)
- Examples
- FAQ
 
## Getting Started
 
For step-by-step build and test instructions (Docker/WSL2), see [BUILD+TEST.MD](../BUILD+TEST.MD).
 
Key commands:
 
```bash
# Docker tests
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test ."
```
 
Docker demo:
 
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull --load -t mcp-cpp-demo ."
wsl -d Ubuntu -- bash -lc "docker run --rm --name mcp-cpp-demo --mount type=bind,src=/mnt/c/Work/mcp-cpp,dst=/work mcp-cpp-demo"
``` 

## API Overview

For details on commonly used APIs, see the dedicated sections in the root README:
- Subscriptions (per-URI): [README.md#resource-subscriptions](../README.md#resource-subscriptions)
- Paging (tools/resources/prompts/templates): [README.md#paging](../README.md#paging)
- Sampling handler (server): [README.md#server-side-sampling](../README.md#server-side-sampling)

## API Reference

Detailed per-interface API documentation:
- Client API: [docs/api/client.md](./api/client.md)
- Server API: [docs/api/server.md](./api/server.md)
- Transport API: [docs/api/transport.md](./api/transport.md)
 - Authentication: [docs/api/auth.md](./api/auth.md)

## Experimental capabilities

The MCP specification allows non-standard extensions to be advertised under `capabilities.experimental` during the initialize handshake. These keys are optional, subject to change, and should be safely ignored by clients that do not recognize them.

- What this means:
  - Server and client may advertise additional feature flags or hints via `capabilities.experimental`.
  - Unknown experimental keys must be ignored by the peer; features should degrade gracefully when absent.

- Used in this SDK:
  - `capabilities.experimental.keepalive` (server → client): see [Keepalive](./api/server.md#keepalive--heartbeat)
  - `capabilities.experimental.loggingRateLimit` (server → client): see [Logging rate limiting](./api/server.md#logging-rate-limiting-experimental)
  - `capabilities.experimental.logLevel` (client → server minimum log level): see [Logging to client](./api/server.md#logging-to-client)
  - `capabilities.experimental.resourceReadChunking` (server → client): see [Resource read chunking](./api/server.md#resource-read-chunking-experimental)

These experimental keys are a standard extension mechanism within MCP and do not change the core protocol messages; they provide optional behaviors that can evolve without breaking existing clients.

## Environment variables

These environment variables influence runtime/demo behavior. See [BUILD+TEST.MD](../BUILD+TEST.MD#demo-and-transport-options-env--factory-config) for examples.

- `MCP_STDIOTRANSPORT_TIMEOUT_MS`: Default request timeout for `StdioTransport` (milliseconds). `0` disables.
- `MCP_STDIO_CONFIG`: Pass stdio transport options as a `key=value` list (e.g., `timeout_ms`, `idle_read_timeout_ms`, `write_timeout_ms`, `write_queue_max_bytes`).
- `MCP_KEEPALIVE_MS`: Enable periodic server keepalive notifications in the demo.

## Examples

- `examples/mcp_server` and `examples/mcp_client`: Demo server and client wired over stdio (used by `scripts/run_demo.sh`).
- `examples/subscriptions_progress`: Demonstrates per-URI resource subscriptions and server progress notifications using `InMemoryTransport`.
- `examples/stdio_smoke`: Minimal Windows-native smoke that exercises `StdioTransport` start/stop paths.
- `examples/resource_chunking`: Demonstrates experimental resource range reads and reassembly via typed wrappers.
- `examples/sampling_roundtrip`: Demonstrates server-initiated sampling using a typed helper result (`mcp/typed/Sampling.h`).
- `examples/logging_demo`: Demonstrates server logging to client with minimum log level filtering and rate limiting.
- `examples/keepalive_demo`: Demonstrates keepalive notifications and failure handling.

## Coding style checklist
{{ ... }}

- Use C++20 and avoid busy loops; prefer async, futures/promises, and coroutines.
- No third-party runtime deps; standard library and C-runtime only.
- Every source file must include the SPDX header banner with filename and purpose.
- Follow the Google C++ Style Guide: 
https://google.github.io/styleguide/cppguide.html#C++_Version
- Add comprehensive GoogleTest coverage for new changes, including negative paths; register tests in `tests/CMakeLists.txt`.
