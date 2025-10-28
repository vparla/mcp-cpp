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

### Full suite (unit/integration + HTTPS e2e)

Run the entire test suite including the two-node HTTPS e2e tests. This script:

- Builds `Dockerfile.demo` test target and runs `ctest` (verbose).
- Builds the HTTPS e2e image in `tests/http/`.
- Runs two containers (`http-server`, `http-client`) via `docker-compose` and streams logs.

- Linux/macOS:
```bash
cd /path/to/mcp-cpp
chmod +x scripts/*.sh
./scripts/ci_all.sh
```

- Windows (PowerShell via WSL2 Ubuntu):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && chmod +x scripts/*.sh && ./scripts/ci_all.sh"
```

Notes:
- The script shows a complete test list (`ctest -N`) and then runs all tests (`ctest -VV --progress`).
- HTTPS e2e logs from both server and client containers are streamed to the terminal.
- Set `MCP_LOG_LEVEL=DEBUG` to increase e2e verbosity.

Tips:
- Add `--no-cache` to force a fresh test run.
- CTest output will display pass/fail for each test. Example tests live in `tests/` (e.g., `tests/test_inmemory_transport.cpp`).
- Add new tests by updating `tests/CMakeLists.txt` and adding `TEST(...)` cases.
- Note: when writing code that directly uses the in-memory transport, include `mcp/InMemoryTransport.hpp`. For StdioTransport, include `mcp/StdioTransport.hpp`.

## Debugging with GDB (Docker-first)

Use a Docker-first workflow to capture backtraces without installing native tools on the host.

- Build the compiled image (examples + tests)
  - Windows (PowerShell via WSL2 Ubuntu):
  ```powershell
  wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target build --progress=plain --pull --load -t mcp-cpp-build ."
  ```

- Create a debug image with GDB preinstalled
  - Windows (PowerShell via WSL2 Ubuntu):
  ```powershell
  wsl -d Ubuntu -- bash -lc 'cat > /tmp/Dockerfile.mcpdbg << "EOF"\nFROM mcp-cpp-build\nRUN apt-get update -qq && apt-get install -y -qq --no-install-recommends gdb && rm -rf /var/lib/apt/lists/*\nEOF\n\ndocker build -t mcp-cpp-build-dbg -f /tmp/Dockerfile.mcpdbg .'
  ```

- Run a targeted test inside the debug image (optional quick check)
  ```powershell
  wsl -d Ubuntu -- bash -lc "docker run --rm -t --ipc=host mcp-cpp-build-dbg bash -lc 'cd /src && CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir build -R TransportDemo.Run -VV --progress --timeout 180'"
  ```

- Dump a backtrace for the HTTP demo (prints in your terminal)
  - Launches the server in the background and runs the client under GDB with a script that prints the full stack on faults.
  ```powershell
  wsl -d Ubuntu -- bash -lc 'docker run --rm -it --ipc=host --cap-add=SYS_PTRACE --security-opt seccomp=unconfined mcp-cpp-build-dbg bash -lc "set -euo pipefail; printf \\\"set pagination off\\nhandle SIGPIPE nostop noprint pass\\nhandle SIGSEGV stop print pass\\nrun\\nbt full\\nthread apply all bt full\\nquit\\n\\\" > /tmp/gdb.cmd; /src/build/examples/mcp_server/mcp_server --transport=http --listen=http://127.0.0.1:9443 >/dev/null 2>&1 & spid=\\\$!; sleep 0.6; gdb -q -x /tmp/gdb.cmd --args /src/build/examples/mcp_client/mcp_client --transport=http --url=http://127.0.0.1:9443; code=\\\$?; kill -TERM \\\$spid 2>/dev/null || true; wait \\\$spid 2>/dev/null || true; exit \\\$code"'
  ```

Notes:
- Ensure examples exist under `/src/build/examples/...` by using `Dockerfile.demo --target build` (`mcp-cpp-build`).
- `--cap-add=SYS_PTRACE --security-opt seccomp=unconfined` lets GDB run inside Docker.
- If prompted about debuginfod, answering `n` is fine (optional).

- Debug any GoogleTest case (tests/mcp_tests)
  - List matching tests:
  ```powershell
  wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -N -R ClientPaging'"
  ```
  - Run a specific test under GDB (replace the filter with your `Suite.Test`):
  ```powershell
  wsl -d Ubuntu -- bash -lc "docker run --rm -it mcp-cpp-build-dbg bash -lc 'printf \"set pagination off\nhandle SIGPIPE nostop noprint pass\nrun\nbt full\nthread apply all bt full\nquit\n\" > /tmp/gdb.cmd; gdb -q -x /tmp/gdb.cmd --args /src/build/tests/mcp_tests --gtest_filter=ClientPaging.ToolsListPaged'"
  ```

- Debug any CTest test (generic)
  - Show the exact command CTest uses:
  ```powershell
  wsl -d Ubuntu -- bash -lc "docker run --rm -t mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -N -V -R TransportDemo.Run'"
  ```
  - If it invokes `mcp_tests --gtest_filter=...`, reuse the previous GoogleTest pattern.
  - If it runs a script (e.g., `scripts/run_demo.sh`), open the script and wrap the target binary with GDB as needed (e.g., `mcp_client`, `mcp_server`), starting any prerequisites in the background.

- Helper script: quick HTTP GDB session
  - Script: `scripts/_gdb_http_debug.sh`
  - What it does: rebuilds `mcp-cpp-build`, layers `mcp-cpp-build-dbg` with GDB, starts the HTTP server, then runs the client under GDB and prints a backtrace on faults.
  - Usage (WSL PowerShell):
  ```powershell
  wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && chmod +x scripts/_gdb_http_debug.sh && ./scripts/_gdb_http_debug.sh --no-cache --url http://127.0.0.1:9443"
  ```
  - Options:
    - `--no-cache` forces fresh rebuilds of both images.
    - `--url <http://host:port>` overrides the listen/target URL (default `http://127.0.0.1:9443`).

- Optional: rebuild with symbols (RelWithDebInfo)
  ```powershell
  wsl -d Ubuntu -- bash -lc "docker run --rm -it --ipc=host mcp-cpp-build-dbg bash -lc 'set -euo pipefail; cmake -S /src -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMCP_BUILD_TESTS=ON; cmake --build /src/build -j; cd /src && CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir build -R TransportDemo.Run -VV --progress --timeout 180'"
  ```

## Notes
- The script shows a complete test list (`ctest -N`) and then runs all tests (`ctest -VV --progress`).
- HTTPS e2e logs from both server and client containers are streamed to the terminal.
- Set `MCP_LOG_LEVEL=DEBUG` to increase e2e verbosity.

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

#### Shared-memory transport in Docker (host IPC recommended)

`SharedMemoryTransport` uses Boost.Interprocess `message_queue` (POSIX mqueues). The most portable way to expose mqueues inside Docker is to use the host IPC namespace.

- Recommended: host IPC (no special mounts, non‑privileged)

  - Linux/macOS (native):
  ```bash
  docker run --rm -t \
    --ipc=host \
    mcp-cpp-build \
    bash -lc 'cd /src && ctest --test-dir build -R TransportDemo.Run -VV --output-on-failure'
  ```

  - Windows (PowerShell via WSL2 Ubuntu):
  ```powershell
  wsl -d Ubuntu -- bash -lc "docker run --rm -t --ipc=host mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R TransportDemo.Run -VV --output-on-failure'"
  ```

- Alternatives (if host IPC isn’t available):
  - Mount mqueue explicitly (works on many native Linux setups):
    ```bash
    docker run --rm -t \
      --mount type=mqueue,target=/dev/mqueue \
      mcp-cpp-build \
      bash -lc 'cd /src && ctest --test-dir build -R TransportDemo.Run -VV --output-on-failure'
    ```
  - Privileged container and mount inside (least preferred):
    ```powershell
    wsl -d Ubuntu -- bash -lc "docker run --rm -t --privileged mcp-cpp-build bash -lc 'mount -t mqueue none /dev/mqueue 2>/dev/null || true; cd /src && ctest --test-dir build -R TransportDemo.Run -VV --output-on-failure'"
    ```

Notes:
- Docker build stages cannot mount host devices. The `--target test` step in `Dockerfile.demo` cannot mount `/dev/mqueue`. Use the runtime commands above to run SHM‑dependent demos/tests, or skip them during build‑time testing.
- The ALL script `scripts/ci_all.sh` now runs tests with `--ipc=host`.

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
- __TransportDemo.Run__ — demo integration that exercises stdio, shared-memory, and HTTP transports; builds and runs the example server and client. Configurable via `MCP_STDIOTRANSPORT_TIMEOUT_MS`. Defined in [tests/CMakeLists.txt](c:/Work/mcp-cpp/tests/CMakeLists.txt) and script [scripts/run_demo.sh](c:/Work/mcp-cpp/scripts/run_demo.sh)

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
wsl -d Ubuntu -- bash -lc "docker run --rm -t --mount type=mqueue,target=/dev/mqueue mcp-cpp-build bash -lc 'cd /src && ctest --test-dir build -R TransportDemo.Run -VV --output-on-failure'"
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

To enable the Shared-memory transport demo inside the container, mount `/dev/mqueue`:

```bash
docker run --rm --name mcp-cpp-demo \
  --mount type=mqueue,target=/dev/mqueue \
  mcp-cpp-demo
```

- Windows (PowerShell via WSL2 Ubuntu):
```powershell
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Work/mcp-cpp && docker buildx build -f Dockerfile.demo --target demo --progress=plain --pull --load -t mcp-cpp-demo ."
wsl -d Ubuntu -- bash -lc "docker run --rm --name mcp-cpp-demo -e MCP_STDIOTRANSPORT_TIMEOUT_MS=2000 --mount type=bind,src=/mnt/c/Work/mcp-cpp,dst=/work mcp-cpp-demo"
```

To enable the Shared-memory transport demo on Windows (WSL), add the mqueue mount:

```powershell
wsl -d Ubuntu -- bash -lc "docker run --rm --name mcp-cpp-demo --mount type=mqueue,target=/dev/mqueue mcp-cpp-demo"
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

Notes:
- Ensure examples exist under `/src/build/examples/...` by using `Dockerfile.demo --target build` (`mcp-cpp-build`).
- `--cap-add=SYS_PTRACE --security-opt seccomp=unconfined` lets GDB run inside Docker.
- If prompted about debuginfod, answering `n` is fine (optional).

- **Optional: rebuild with symbols (RelWithDebInfo)**
  ```powershell
  wsl -d Ubuntu -- bash -lc "docker run --rm -it --ipc=host mcp-cpp-build-dbg bash -lc 'set -euo pipefail; cmake -S /src -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMCP_BUILD_TESTS=ON; cmake --build /src/build -j; cd /src && CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir build -R TransportDemo.Run -VV --progress --timeout 180'"
  ```

## Notes
 
 - All instructions above are cross-platform via Docker. No native toolchain setup is required on the host.
 - For CI/CD, reuse the same `--target test` to gate merges.
