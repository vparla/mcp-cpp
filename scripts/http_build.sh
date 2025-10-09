#!/usr/bin/env bash
set -euo pipefail

# Build the HTTP(S) e2e test image (server + client tests)
# Usage: ./scripts/http_build.sh

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

echo "[http_build] Building mcp-http image using local base 'mcp-cpp-build'..."
docker build \
  -f tests/http/Dockerfile \
  --build-arg BUILD_IMAGE=mcp-cpp-build \
  -t mcp-http \
  .

echo "[http_build] Done. Image tag: mcp-http"
