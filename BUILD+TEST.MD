<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: BUILD+TEST.MD
Purpose: Build and test documentation (Docker-first workflow, options, and tips)
==========================================================================================================
-->

# Build and Test (Docker-first)

This project uses a Docker-first workflow for reproducible builds and tests across Linux, macOS, and Windows.

- Buildx multi-stage images provide:
  - build (CMake/Ninja)
  - test (GoogleTest via CTest)
  - demo (client/server example)
  - artifacts (headers + static library)
- On Windows, use WSL2 Ubuntu to invoke Docker commands.

## Prerequisites

- Docker Engine with Buildx
- Windows only: WSL2 Ubuntu installed and Docker Desktop or Docker Engine in WSL
- Project path on Windows: `C:\Work\mcp-cpp` (mounted at `/mnt/c/Work/mcp-cpp` inside WSL)

Verify Docker:
```bash
# Linux/macOS terminal, or WSL Ubuntu
docker version
```

## Run unit tests (GoogleTest) inside Docker

GoogleTest is fetched at CMake configure time (via FetchContent) and compiled only in the test image. It is not committed to this repo and not part of runtime artifacts.

- Linux/macOS:
```bash
cd /path/to/mcp-cpp
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test .
```

- Windows (PowerShell via WSL2 Ubuntu):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test ."
```

Tips:
- Add `--no-cache` to force a fresh test run.
- CTest output will display pass/fail for each test. Example tests live in `tests/` (e.g., `tests/test_inmemory_transport.cpp`).
- Add new tests by updating `tests/CMakeLists.txt` and adding `TEST(...)` cases.
- Note: when writing code that directly uses the in-memory transport, include `mcp/InMemoryTransport.hpp`. For StdioTransport, include `mcp/StdioTransport.hpp`.

### Terminal output and colors

- The Docker test stage runs `ctest -VV`, so GoogleTest logs and any shell script output are printed to your terminal.
- The demo script [`scripts/run_demo.sh`](./scripts/run_demo.sh) prints `[demo] ...` lines to stderr. You can disable color with `DEMO_COLOR=0`.
- The stdio hardening tests [`scripts/test_stdio_hardening.sh`](./scripts/test_stdio_hardening.sh) print concise `[OK]/[FAIL]` status lines.

### Test cases and what they cover

We write tests with GoogleTest, and CTest (from CMake) discovers and runs them inside the `--target test` Docker image (see `tests/CMakeLists.txt` and `Dockerfile.demo`). The list below summarizes each test; the sources live under `tests/`:

- __InMemoryTransport.RequestResponseRoutes__ — basic request/response over in-memory transport. Validates request handling and result propagation. Source: [tests/test_inmemory_transport.cpp](c:/Work/mcp-cpp/tests/test_inmemory_transport.cpp)
- __InMemoryTransport.ErrorWhenPeerDisconnected__ — sending a request after the peer disconnects yields an error. Source: [tests/test_inmemory_transport.cpp](c:/Work/mcp-cpp/tests/test_inmemory_transport.cpp)
- __InMemoryTransport.NotificationRouting__ — notifications are delivered to the correct handler. Source: [tests/test_inmemory_transport.cpp](c:/Work/mcp-cpp/tests/test_inmemory_transport.cpp)
- __InMemoryTransport.PendingRequestsFailOnClose__ — pending requests complete with error if the client closes. Source: [tests/test_inmemory_transport.cpp](c:/Work/mcp-cpp/tests/test_inmemory_transport.cpp)
- __ServerResourceTemplates.ListRoundTrip__ — server registers resource templates and client lists them end-to-end. Source: [tests/test_resource_templates.cpp](c:/Work/mcp-cpp/tests/test_resource_templates.cpp)
- __PromptsGet.ReturnsActualMessages__ — `prompts/get` returns actual `messages` content, not placeholders. Source: [tests/test_prompts_get.cpp](c:/Work/mcp-cpp/tests/test_prompts_get.cpp)
- __ServerToolsInputSchema.ListIncludesProvidedSchema__ — validates Step 1 change: registering a tool with metadata (description + inputSchema) is reflected by `tools/list`. Source: [tests/test_tools_inputschema.cpp](c:/Work/mcp-cpp/tests/test_tools_inputschema.cpp)
- __ClientPaging.ToolsListPaged__ — verifies paging for tools/list with cursor and limit. Source: [tests/test_client_paging.cpp](c:/Work/mcp-cpp/tests/test_client_paging.cpp)
- __ClientPaging.ResourcesListPaged__ — verifies paging for resources/list. Source: [tests/test_client_paging.cpp](c:/Work/mcp-cpp/tests/test_client_paging.cpp)
- __ClientPaging.ResourceTemplatesListPaged__ — verifies paging for resources/templates/list. Source: [tests/test_client_paging.cpp](c:/Work/mcp-cpp/tests/test_client_paging.cpp)
- __ClientPaging.PromptsListPaged__ — verifies paging for prompts/list. Source: [tests/test_client_paging.cpp](c:/Work/mcp-cpp/tests/test_client_paging.cpp)
- __ServerReadResource.ReturnsContents__ — server ReadResource returns contents array with expected shape. Source: [tests/test_read_resource.cpp](c:/Work/mcp-cpp/tests/test_read_resource.cpp)
- __ServerSampling.HandlesCreateMessageRequest__ — server handles sampling/createMessage with a registered handler. Source: [tests/test_sampling_handler.cpp](c:/Work/mcp-cpp/tests/test_sampling_handler.cpp)
- __ResourceSubscriptions.FiltersUpdatesByUri__ — server filters resources/updated notifications by subscribed URIs. Source: [tests/test_resource_subscriptions.cpp](c:/Work/mcp-cpp/tests/test_resource_subscriptions.cpp)
- __ClientSubscribeUri.SubAndFilterByUri__ — client API per-URI subscription and filtered notifications. Source: [tests/test_client_subscribe_uri.cpp](c:/Work/mcp-cpp/tests/test_client_subscribe_uri.cpp)
- __StdioDemo.Run__ — demo integration using stdio transport; builds and runs the example server and client, wiring them via FIFOs. Configurable via `MCP_STDIOTRANSPORT_TIMEOUT_MS`. Defined in [tests/CMakeLists.txt](c:/Work/mcp-cpp/tests/CMakeLists.txt) and script [scripts/run_demo.sh](c:/Work/mcp-cpp/scripts/run_demo.sh)

- __StdioHardening.IdleReadTimeout__ — verifies idle read timeout closes transport when no bytes are received for the configured duration. Script: [scripts/test_stdio_hardening.sh](c:/Work/mcp-cpp/scripts/test_stdio_hardening.sh)
- __StdioHardening.WriteQueueOverflow__ — small write queue with frequent keepalives triggers overflow; transport emits error and closes. Script: [scripts/test_stdio_hardening.sh](c:/Work/mcp-cpp/scripts/test_stdio_hardening.sh)
- __StdioHardening.WriteTimeout__ — non-blocking writes stall with no reader; write timeout closes transport. Script: [scripts/test_stdio_hardening.sh](c:/Work/mcp-cpp/scripts/test_stdio_hardening.sh)
- __StdioHardening.BadContentLength__ — oversized Content-Length header is rejected; server terminates. Script: [scripts/test_stdio_hardening.sh](c:/Work/mcp-cpp/scripts/test_stdio_hardening.sh)

Run the full suite with no cache (WSL):

```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --no-cache --progress=plain --pull --load -t mcp-cpp-test ."
```

Run a single test (advanced, WSL):

```powershell
# Build the compiled image without running ctest automatically
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target build --progress=plain --pull --load -t mcp-cpp-build ."

# Run just one test by name (regex), here the Step 1 test
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R ServerToolsInputSchema.ListIncludesProvidedSchema --output-on-failure'"
```

Cheat sheet: run any single test (WSL)

First build the container once (as above), then pick one:

```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R InMemoryTransport.RequestResponseRoutes --output-on-failure'"
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R InMemoryTransport.ErrorWhenPeerDisconnected --output-on-failure'"
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R InMemoryTransport.NotificationRouting --output-on-failure'"
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R InMemoryTransport.PendingRequestsFailOnClose --output-on-failure'"
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R ServerResourceTemplates.ListRoundTrip --output-on-failure'"
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R PromptsGet.ReturnsActualMessages --output-on-failure'"
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R ServerToolsInputSchema.ListIncludesProvidedSchema --output-on-failure'"
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R StdioDemo.Run --output-on-failure'"
```

Run only stdio hardening tests (WSL):

```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R StdioHardening --output-on-failure'"
```

### Configurable request timeout for StdioTransport

`StdioTransport` supports a configurable request timeout via environment variable and programmatic setter.

- Default is 30000 ms. Setting `0` disables timeouts.
- Env var: `MCP_STDIOTRANSPORT_TIMEOUT_MS` (propagated into the test stage).
- Programmatic: `transport->SetRequestTimeoutMs(<ms>);`
- Factory config: `StdioTransportFactory().CreateTransport("timeout_ms=<ms>")`
- Header for direct use: include `mcp/StdioTransport.hpp`

Set via Docker Buildx (tests):

- Linux/macOS:
```bash
docker buildx build \
  -f Dockerfile.demo \
  --target test \
  --build-arg MCP_STDIOTRANSPORT_TIMEOUT_MS=200 \
  --progress=plain --pull --load -t mcp-cpp-test .
```

- Windows (PowerShell via WSL2 Ubuntu):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --build-arg MCP_STDIOTRANSPORT_TIMEOUT_MS=200 --progress=plain --pull --load -t mcp-cpp-test ."
```

## Build and run the demo

Runs a server and client connected over stdio; logs are routed to stderr to avoid corrupting frames.

- Linux/macOS:
```bash
cd /path/to/mcp-cpp
docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull --load -t mcp-cpp-demo .
docker run --rm --name mcp-cpp-demo \
  -e MCP_STDIOTRANSPORT_TIMEOUT_MS=2000 \
  --mount type=bind,src=$(pwd),dst=/work mcp-cpp-demo
```

- Windows (PowerShell via WSL2 Ubuntu):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull --load -t mcp-cpp-demo ."
wsl -d Ubuntu -- bash -lc "docker run --rm --name mcp-cpp-demo -e MCP_STDIOTRANSPORT_TIMEOUT_MS=2000 --mount type=bind,src=/mnt/c/Work/mcp-cpp,dst=/work mcp-cpp-demo"
```

Troubleshooting:
- If the container name is already in use: `docker rm -f mcp-cpp-demo` and re-run.

### Demo and transport options (env + factory config)

- To pass stdio transport options to the demo server, set `MCP_STDIO_CONFIG`:
  - Recognized keys: `timeout_ms`, `idle_read_timeout_ms`, `write_timeout_ms`, `write_queue_max_bytes`.
  - Example (Linux/macOS):
    ```bash
    docker run --rm --name mcp-cpp-demo \
      -e MCP_STDIO_CONFIG="timeout_ms=30000;idle_read_timeout_ms=200" \
      mcp-cpp-demo
    ```
  - Example (WSL PowerShell):
    ```powershell
    wsl -d Ubuntu -- bash -lc "docker run --rm --name mcp-cpp-demo -e MCP_STDIO_CONFIG='timeout_ms=30000;idle_read_timeout_ms=200' mcp-cpp-demo"
    ```

- To enable periodic keepalive notifications (useful for exercising write-side pressure):
  - `MCP_KEEPALIVE_MS=<interval>`
  - Example: `-e MCP_KEEPALIVE_MS=1` to send frequent keepalives.

- Demo color control:
  - `DEMO_COLOR=0` disables colored `[demo]` lines in `run_demo.sh`.

## Export build artifacts (headers + static lib)

Use the root `Dockerfile` `artifacts` stage to export headers, static lib, and an example binary to a local folder.

- Linux/macOS:
```bash
cd /path/to/mcp-cpp
docker buildx build --target artifacts --output type=local,dest=./out .
```

- Windows (PowerShell via WSL2 Ubuntu):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build --target artifacts --output type=local,dest=/mnt/c/Work/mcp-cpp/out ."
```

Artifacts appear under `out/`:
- `out/include/`
- `out/lib/libmcp_cpp.a`
- `out/bin/mcp_basic`

## About GoogleTest usage

- Integrated via CMake FetchContent in `tests/CMakeLists.txt`.
- Not committed to the repository; fetched at configure time in the test stage.
- Test binary is `tests/mcp_tests` inside the container; ctest discovers and runs tests.

To add tests:
- Create `tests/your_test.cpp` with GoogleTest `TEST(...)` cases.
- Append the file to `add_executable(mcp_tests ...)` in `tests/CMakeLists.txt` or keep using a single test TU and add cases there.
- Re-run the Docker test target.

## Notes

- All instructions above are cross-platform via Docker. No native toolchain setup is required on the host.
- For CI/CD, reuse the same `--target test` to gate merges.
