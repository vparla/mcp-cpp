#!/usr/bin/env bash
#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: scripts/run_server_conformance.sh
# Purpose: Build and run the official MCP server conformance suite against the repo's Dockerized conformance server
#==========================================================================================================
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

build_image="${MCP_CONFORMANCE_BUILD_IMAGE:-mcp-cpp-build}"
server_container="${MCP_CONFORMANCE_SERVER_CONTAINER:-mcp-cpp-conformance-server}"
server_port="${MCP_CONFORMANCE_SERVER_PORT:-3001}"
server_url="${MCP_CONFORMANCE_SERVER_URL:-http://127.0.0.1:${server_port}/mcp}"

cleanup() {
  docker logs "$server_container" >/dev/null 2>&1 || true
  docker rm -f "$server_container" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[conformance] Building Dockerfile.demo build stage"
docker buildx build \
  -f Dockerfile.demo \
  --target build \
  --progress=plain \
  --pull \
  --load \
  -t "$build_image" .

docker rm -f "$server_container" >/dev/null 2>&1 || true

echo "[conformance] Starting conformance server container on ${server_url}"
docker run -d \
  --rm \
  --name "$server_container" \
  --network host \
  "$build_image" \
  bash -lc "/src/build/examples/conformance_server/conformance_server --address=127.0.0.1 --port=${server_port} --endpointPath=/mcp"

echo "[conformance] Waiting for server readiness"
ready=0
for _ in $(seq 1 50); do
  if bash -lc "exec 3<>/dev/tcp/127.0.0.1/${server_port}" >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 0.2
done

if [[ "$ready" -ne 1 ]]; then
  echo "[conformance] Server did not become ready on port ${server_port}" >&2
  docker logs "$server_container" || true
  exit 1
fi

echo "[conformance] Running official MCP server conformance suite"
docker run --rm \
  --network host \
  node:22-bookworm \
  bash -lc "npx --yes @modelcontextprotocol/conformance server --url ${server_url} --suite active"

echo "[conformance] MCP server conformance suite completed successfully"
