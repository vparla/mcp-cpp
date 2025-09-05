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
- Resource Subscriptions (Client API): see [README.md#resource-subscriptions](../README.md#resource-subscriptions)
 - Paging (Client API): see [README.md#paging](../README.md#paging)
 - Server-side Sampling (Server API): see [README.md#server-side-sampling](../README.md#server-side-sampling)
- Keepalive & Logging (Server API): see [docs/api/server.md](./api/server.md#keepalive--heartbeat) and [docs/api/server.md](./api/server.md#logging-to-client)
- Parity Matrix: see [docs/parity-matrix.md](./parity-matrix.md)
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

## Environment variables

These environment variables influence runtime/demo behavior. See [BUILD+TEST.MD](../BUILD+TEST.MD#demo-and-transport-options-env--factory-config) for examples.

- `MCP_STDIOTRANSPORT_TIMEOUT_MS`: Default request timeout for `StdioTransport` (milliseconds). `0` disables.
- `MCP_STDIO_CONFIG`: Pass stdio transport options as a `key=value` list (e.g., `timeout_ms`, `idle_read_timeout_ms`, `write_timeout_ms`, `write_queue_max_bytes`).
- `MCP_KEEPALIVE_MS`: Enable periodic server keepalive notifications in the demo.
- `DEMO_COLOR`: Set to `0` to disable colored output in demo scripts.

## Examples

- `examples/mcp_server` and `examples/mcp_client`: Demo server and client wired over stdio (used by `scripts/run_demo.sh`).
- `examples/subscriptions_progress`: Demonstrates per-URI resource subscriptions and server progress notifications using `InMemoryTransport`.
- `examples/stdio_smoke`: Minimal Windows-native smoke that exercises `StdioTransport` start/stop paths.

## Coding style checklist

Follow the project style rules in `task.md` and the Google C++ Style Guide:

- Use C++20 and avoid busy loops; prefer async, futures/promises, and coroutines.
- No third-party runtime deps; standard library and C-runtime only.
- Every source file must include the SPDX header banner with filename and purpose.
- Brace style and formatting per `task.md`; see Google C++ Style Guide for general rules: https://google.github.io/styleguide/cppguide.html#C++_Version
- Add comprehensive GoogleTest coverage for new changes, including negative paths; register tests in `tests/CMakeLists.txt`.
