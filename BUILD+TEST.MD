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

## Command variations and options (Linux/macOS and Windows via WSL)

Below are commonly used Docker Buildx/CTest variants with both native (Linux/macOS) and Windows (PowerShell via WSL2 Ubuntu) forms. Substitute your WSL distro name if not `Ubuntu`.

### Full test run (default)

- Linux/macOS:
```bash
cd /path/to/mcp-cpp
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test .
```

- Windows (PowerShell via WSL2 Ubuntu):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test ."
```

### Full rebuild (disable layer cache)

- Linux/macOS:
```bash
docker buildx build -f Dockerfile.demo --target test --no-cache --progress=plain --pull --load -t mcp-cpp-test .
```

- Windows (WSL):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target test --no-cache --progress=plain --pull --load -t mcp-cpp-test ."
```

### Build only (don’t run CTest yet)

- Linux/macOS:
```bash
docker buildx build -f Dockerfile.demo --target build --progress=plain --pull --load -t mcp-cpp-build .
```

- Windows (WSL):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target build --progress=plain --pull --load -t mcp-cpp-build ."
```

### Run CTest inside the built container

- Run entire suite with verbose failure output:
```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build --output-on-failure'"
```

- Run tests matching a regex (single or group):
```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R ClientCache --output-on-failure'"
```

- Re-run only failed tests from the last run:
```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build --rerun-failed --output-on-failure'"
```

- Increase verbosity (`-V` or `-VV`) and run in parallel (adjust `-j`):
```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -VV -j 4 --output-on-failure'"
```

- List discovered tests without running:
```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -N'"
```

- Inspect the last CTest log:
```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && sed -n \"1,200p\" build/Testing/Temporary/LastTest.log'"
```

- Start an interactive shell inside the build image:
```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -it mcp-cpp-build bash"
```

> Note: On Linux/macOS, replace the outer `wsl -d Ubuntu -- bash -lc "..."` with the inner command only.

### Specify architecture (e.g., Apple Silicon building x86_64)

- Linux/macOS:
```bash
docker buildx build --platform linux/amd64 -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test .
```

- Windows (WSL):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build --platform linux/amd64 -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test ."
```

### Pass environment/options to tests

- Build-time transport timeout (already supported in Dockerfile):
```bash
docker buildx build -f Dockerfile.demo --target test --build-arg MCP_STDIOTRANSPORT_TIMEOUT_MS=200 --progress=plain --pull --load -t mcp-cpp-test .
```

- Run-time environment inside tests (logger level, stdio mode, etc.):
```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm -t -e MCP_LOG_LEVEL=INFO -e MCP_STDIO_MODE=1 mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build --output-on-failure'"
```

Common env vars:
- `MCP_LOG_LEVEL` (e.g., `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`)
- `MCP_STDIO_MODE=1` routes Logger to stderr (useful to avoid polluting stdio frames)
- `MCP_STDIOTRANSPORT_TIMEOUT_MS` (build arg) configures request timeout

### Clean up Docker caches (optional)

- Remove dangling build cache:
```bash
docker builder prune -f
```

- Remove dangling images:
```bash
docker image prune -f
```

### Docker Desktop (Windows, no WSL wrapper)

If you use Docker Desktop with Linux containers, you can run commands natively in PowerShell without the WSL shim. Use Windows-style paths and prefer `--mount` for bind mounts.

- Full test run:
```powershell
cd C:\Work\mcp-cpp
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull --load -t mcp-cpp-test .
```

- Build only:
```powershell
docker buildx build -f Dockerfile.demo --target build --progress=plain --pull --load -t mcp-cpp-build .
```

- Run full test suite inside built image:
```powershell
docker run --rm -t mcp-cpp-build bash -lc "cd /src && ctest --test-dir build --output-on-failure"
```

- Regex match a test group:
```powershell
docker run --rm -t mcp-cpp-build bash -lc "cd /src && ctest --test-dir build -R ClientCache --output-on-failure"
```

- Run the demo with a bind mount (note Windows path):
```powershell
docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull --load -t mcp-cpp-demo .
docker run --rm --name mcp-cpp-demo \
  -e MCP_STDIOTRANSPORT_TIMEOUT_MS=2000 \
  --mount type=bind,src=C:\\Work\\mcp-cpp,dst=/work mcp-cpp-demo
```

Notes:
- If your Windows path contains spaces, wrap it in quotes: `src="C:\\Path With Spaces\\mcp-cpp"`.
- On older Docker Desktop versions, you may use `-v C:\\Work\\mcp-cpp:/work` instead of `--mount`.
- For multi-arch images on Docker Desktop, set `--platform` as shown below.

### Publishing images to a registry (`--push`)

Use `--push` to upload images to a registry (Docker Hub, GHCR, etc.). Do not combine `--push` with `--load`.

1) Log in to your registry (example: Docker Hub or GHCR):
```powershell
docker login   # Docker Hub
# or
docker login ghcr.io  # GitHub Container Registry
```

2) Choose a tag with your registry/namespace (examples):
- Docker Hub: `yourname/mcp-cpp-test:latest`
- GHCR: `ghcr.io/yourorg/mcp-cpp-test:latest`

3) Build and push a single-arch image:
- Linux/macOS:
```bash
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull \
  -t yourname/mcp-cpp-test:latest --push .
```
- Windows (PowerShell / Docker Desktop):
```powershell
docker buildx build -f Dockerfile.demo --target test --progress=plain --pull \
  -t yourname/mcp-cpp-test:latest --push .
```

4) Build and push a multi-arch image (recommended for public images):
- Linux/macOS:
```bash
docker buildx build --platform linux/amd64,linux/arm64 -f Dockerfile.demo --target test \
  --progress=plain --pull -t yourname/mcp-cpp-test:latest --push .
```
- Windows (PowerShell / Docker Desktop):
```powershell
docker buildx build --platform linux/amd64,linux/arm64 -f Dockerfile.demo --target test \
  --progress=plain --pull -t yourname/mcp-cpp-test:latest --push .
```

5) Optional: ensure a Buildx builder instance is available (only if needed):
```powershell
docker buildx ls
# If no usable builder is available:
docker buildx create --name mcp-builder --use
docker buildx inspect --bootstrap
```

6) Push other targets (build/demo) with your registry tag:
```powershell
# Build stage as a published image
docker buildx build -f Dockerfile.demo --target build --progress=plain --pull \
  -t yourname/mcp-cpp-build:latest --push .

# Demo stage as a published image
docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull \
  -t yourname/mcp-cpp-demo:latest --push .
```

### Terminal output and colors

- The Docker test stage runs `ctest -VV`, so GoogleTest logs and any shell script output are printed to your terminal.
- The demo script [`scripts/run_demo.sh`](./scripts/run_demo.sh) prints `[demo] ...` lines to stderr. You can disable color with `DEMO_COLOR=0`.
- Logger color and output device:
  - `MCP_LOG_COLOR` controls whether the `Logger` uses ANSI color for level labels (default: `1`).
  - `MCP_STDIO_MODE=1` makes the `Logger` write to stderr instead of stdout to avoid corrupting the stdio JSON-RPC stream. This also ensures colored labels appear in CI logs where stdout may be piped.
  - Moving forward, negative stdio hardening tests export `MCP_STDIO_MODE=1` so that Logger messages are visible (and colored) in CI output while the server's stdout is reserved exclusively for JSON-RPC frames.
  - `MCP_LOG_LEVEL` controls verbosity (`DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`). The demo server respects this env var; default is `INFO`.
  - To reduce noise in stdio hardening tests that generate many keepalive notifications, tests 38 and 39 set `MCP_LOG_LEVEL=INFO`.
- The stdio hardening tests [`scripts/test_stdio_hardening.sh`](./scripts/test_stdio_hardening.sh) print concise `[OK]/[FAIL]` status lines and now export `MCP_STDIO_MODE=1` for clear colored Logger output in CI.

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
