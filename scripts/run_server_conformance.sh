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
runner_image="${MCP_CONFORMANCE_RUNNER_IMAGE:-mcp-cpp-conformance-runner}"
server_port="${MCP_CONFORMANCE_SERVER_PORT:-3001}"
server_bind_address="${MCP_CONFORMANCE_SERVER_BIND_ADDRESS:-127.0.0.1}"
server_publish_host="${MCP_CONFORMANCE_SERVER_PUBLISH_HOST:-127.0.0.1}"
server_url="${MCP_CONFORMANCE_SERVER_URL:-http://${server_publish_host}:${server_port}/mcp}"
suite_name="${MCP_CONFORMANCE_SUITE:-active}"
runner_source="${MCP_CONFORMANCE_RUNNER_SOURCE:-@modelcontextprotocol/conformance@0.1.15}"
skip_build="${MCP_CONFORMANCE_SKIP_BUILD:-0}"
suite_name="${suite_name%$'\r'}"

suite_args=()
if [[ -n "$suite_name" && "$suite_name" != "active" ]]; then
  suite_args+=(--suite="$suite_name")
fi

resolve_runner_source() {
  if [[ -f "$runner_source" ]]; then
    local source_abs
    source_abs="$(cd "$(dirname "$runner_source")" && pwd)/$(basename "$runner_source")"
    case "$source_abs" in
      "$repo_root"/*)
        printf '%s\n' "${source_abs#"$repo_root"/}"
        return 0
        ;;
      *)
        echo "[conformance] Runner package file must live under the repository root: ${runner_source}" >&2
        exit 1
        ;;
    esac
  fi
  printf '%s\n' "$runner_source"
}

wait_for_server() {
  local ready=0
  local probe_command
  probe_command="exec 3<>/dev/tcp/${server_publish_host}/${server_port}"
  for _ in $(seq 1 50); do
    if docker run --rm --network host "$build_image" bash -lc "$probe_command" >/dev/null 2>&1; then
      ready=1
      break
    fi
    sleep 0.2
  done

  if [[ "$ready" -ne 1 ]]; then
    echo "[conformance] Server did not become ready on ${server_publish_host}:${server_port}" >&2
    docker logs "$server_container" || true
    exit 1
  fi
}

build_runner_image_from_tarball() {
  local source_rel="$1"
  local source_name
  source_name="$(basename "$source_rel")"

  echo "[conformance] Building conformance runner image from ${source_rel}"
  docker buildx build \
    --progress=plain \
    --load \
    -t "$runner_image" \
    -f - . <<EOF
FROM node:22-bookworm
WORKDIR /opt/mcp-conformance
COPY ${source_rel} /opt/mcp-conformance/${source_name}
RUN npm init -y >/dev/null 2>&1 \\
    && npm install "./${source_name}" >/dev/null 2>&1
ENTRYPOINT ["npx", "@modelcontextprotocol/conformance"]
EOF
}

run_conformance_from_package_spec() {
  local source_spec="$1"
  echo "[conformance] Running official MCP server conformance suite from ${source_spec}"
  docker run --rm \
    --network host \
    node:22-bookworm \
    bash -lc "set -euo pipefail && mkdir -p /tmp/mcp-conformance && cd /tmp/mcp-conformance && npm init -y >/dev/null 2>&1 && npm install \"${source_spec}\" >/dev/null 2>&1 && npx @modelcontextprotocol/conformance server --url \"${server_url}\" ${suite_args[*]}"
}

run_conformance_from_runner_image() {
  echo "[conformance] Running official MCP server conformance suite from ${runner_image}"
  docker run --rm \
    --network host \
    "$runner_image" \
    server \
    --url "$server_url" \
    "${suite_args[@]}"
}

cleanup() {
  docker logs "$server_container" >/dev/null 2>&1 || true
  docker rm -f "$server_container" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "[conformance] Building Dockerfile.demo build stage"
if [[ "$skip_build" != "1" ]]; then
  docker buildx build \
    -f Dockerfile.demo \
    --target build \
    --progress=plain \
    --pull \
    --load \
    -t "$build_image" .
else
  echo "[conformance] Skipping Dockerfile.demo build stage"
fi

docker rm -f "$server_container" >/dev/null 2>&1 || true

echo "[conformance] Starting conformance server container on ${server_url}"
docker run -d \
  --rm \
  --name "$server_container" \
  --network host \
  "$build_image" \
  bash -lc "/src/build/examples/conformance_server/conformance_server --address=${server_bind_address} --port=${server_port} --endpointPath=/mcp"

echo "[conformance] Waiting for server readiness"
wait_for_server

resolved_source="$(resolve_runner_source)"
if [[ "$resolved_source" == *.tgz ]]; then
  build_runner_image_from_tarball "$resolved_source"
  run_conformance_from_runner_image
else
  run_conformance_from_package_spec "$resolved_source"
fi

echo "[conformance] MCP server conformance suite completed successfully"
