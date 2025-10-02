#!/usr/bin/env bash
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
docker run --rm \
  -e GTEST_COLOR=yes \
  -e CTEST_OUTPUT_ON_FAILURE=1 \
  mcp-cpp-build \
  ctest --test-dir build -N -V

echo "[ci_all] 1c/3 Run unit/integration tests verbosely"
docker run --rm \
  -e GTEST_COLOR=yes \
  -e CTEST_OUTPUT_ON_FAILURE=1 \
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
