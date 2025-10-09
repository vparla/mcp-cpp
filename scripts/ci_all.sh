#!/usr/bin/env bash
#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: ci_all.sh
# Purpose: End-to-end CI driver: builds test image, runs CTest, builds/runs HTTPS e2e
#==========================================================================================================
set -euo pipefail

# End-to-end CI script: build unit/integration tests and run HTTPS e2e
# Usage: ./scripts/ci_all.sh

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

echo "[ci_all] 1/3 Build once (Dockerfile.demo build stage)"
docker buildx build \
  -f Dockerfile.demo \
  --target build \
  --no-cache \
  --progress=plain \
  --pull \
  --load \
  -t mcp-cpp-build \
  .

# Export the built image to a tarball for portable build context consumption
mkdir -p .ci
docker save mcp-cpp-build:latest -o .ci/mcp-cpp-build.tar

echo "[ci_all] 1b/3 List discovered unit/integration tests"
# NOTE: Shared-memory transport (Boost.Interprocess message_queue) relies on POSIX mqueues.
# Use host IPC to expose the host's /dev/mqueue inside the container without special mounts.
docker run --rm \
  -e GTEST_COLOR=yes \
  -e CTEST_OUTPUT_ON_FAILURE=1 \
  --ipc=host \
  mcp-cpp-build \
  ctest --test-dir build -N -V

echo "[ci_all] 1c/3 Run unit/integration tests verbosely"
# Use host IPC for SHM transport tests; avoids mount issues on Docker Desktop/WSL
docker run --rm \
  -e GTEST_COLOR=yes \
  -e CTEST_OUTPUT_ON_FAILURE=1 \
  --ipc=host \
  mcp-cpp-build \
  ctest --test-dir build -VV --progress

echo "[ci_all] 2/3 Build HTTPS e2e image"
chmod +x scripts/http_build.sh
./scripts/http_build.sh

echo "[ci_all] 3/3 Run HTTPS e2e (two-node compose)"
chmod +x scripts/http_e2e.sh scripts/http_down.sh
./scripts/http_e2e.sh || {
  rc=$?
  echo "[ci_all] e2e failed (rc=$rc). Tearing down stack..."
  ./scripts/http_down.sh || true
  exit $rc
}

# Cleanup
./scripts/http_down.sh || true

echo "[ci_all] SUCCESS: All tests passed"
