#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: run_demo.sh
# Purpose: Demo runner: exercises stdio, shared-memory, and HTTP transports in succession
set -euo pipefail

# Optional colorization for demo messages (stderr). Set DEMO_COLOR=0 to disable.
COLOR_ON="${DEMO_COLOR:-1}"
if [[ "$COLOR_ON" == "1" ]]; then
  C_INFO=$'\033[36m'  # cyan
  C_WARN=$'\033[33m'  # yellow
  C_OK=$'\033[32m'    # green
  C_ERR=$'\033[31m'   # red
  C_RST=$'\033[0m'
else
  C_INFO=""; C_WARN=""; C_OK=""; C_ERR=""; C_RST=""
fi

SERVER_BIN="${SERVER_BIN:-/usr/local/bin/mcp_server}"
CLIENT_BIN="${CLIENT_BIN:-/usr/local/bin/mcp_client}"

print_banner() {
  echo "${C_INFO}================================================================================${C_RST}" >&2
  echo "${C_INFO}[demo] $1${C_RST}" >&2
  echo "${C_INFO}================================================================================${C_RST}" >&2
}

run_stdio_demo() {
  print_banner "STDIO transport (FIFOs)"
  local C2S="/tmp/mcp_c2s.fifo"
  local S2C="/tmp/mcp_s2c.fifo"
  rm -f "$C2S" "$S2C" 2>/dev/null || true
  mkfifo "$C2S" "$S2C"
  # Pre-open both FIFOs RDWR to avoid open() blocking on the first writer/reader
  exec 3<>"$C2S"
  exec 4<>"$S2C"
  # Start server: stdin from FD 3 (C2S), stdout to FD 4 (S2C)
  "$SERVER_BIN" --transport=stdio 0<&3 1>&4 2>&2 3<&- 4<&- &
  local SERVER_PID=$!
  sleep 0.2
  # Run client: stdin from FD 4 (S2C), stdout to FD 3 (C2S)
  "$CLIENT_BIN" --transport=stdio 0<&4 1>&3 2>&2 3<&- 4<&-
  local CLIENT_STATUS=$?
  # Close extra RDWR descriptors to allow EOF propagation on FIFOs
  echo "${C_INFO}[demo] Closing FIFO descriptors${C_RST}" >&2
  exec 3>&- || true
  exec 4>&- || true
  # Stop server
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "${C_INFO}[demo] Sending SIGTERM to stdio server (pid=$SERVER_PID)${C_RST}" >&2
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    for i in {1..50}; do
      if ! kill -0 "$SERVER_PID" 2>/dev/null; then break; fi
      sleep 0.1
    done
    if kill -0 "$SERVER_PID" 2>/dev/null; then
      echo "${C_WARN}[demo][WARN] SIGKILL stdio server${C_RST}" >&2
      kill -KILL "$SERVER_PID" 2>/dev/null || true
    fi
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [[ "$CLIENT_STATUS" -eq 0 ]]; then
    echo "${C_OK}[demo][stdio] SUCCESS${C_RST}" >&2
    return 0
  else
    echo "${C_ERR}[demo][stdio] FAILURE (client exit $CLIENT_STATUS)${C_RST}" >&2
    return 1
  fi
}

run_shm_demo() {
  print_banner "Shared-memory transport (Boost.Interprocess message_queue)"
  local CHANNEL="mcp-shm"
  "$SERVER_BIN" --transport=shm --channel="$CHANNEL" 2>&2 &
  local SERVER_PID=$!
  sleep 0.2
  "$CLIENT_BIN" --transport=shm --channel="$CHANNEL" 2>&2
  local CLIENT_STATUS=$?
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    sleep 0.2
    kill -KILL "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [[ "$CLIENT_STATUS" -eq 0 ]]; then
    echo "${C_OK}[demo][shm] SUCCESS${C_RST}" >&2
    return 0
  else
    echo "${C_ERR}[demo][shm] FAILURE (client exit $CLIENT_STATUS)${C_RST}" >&2
    return 1
  fi
}

run_http_demo() {
  print_banner "HTTP transport (localhost loopback)"
  local URL="http://127.0.0.1:9443"
  # HTTP readiness probe: send a minimal POST with JSON body and expect any response line.
  waitForHttp() {
    local host="$1"; local port="$2"; local path="${3:-/mcp/rpc}"; local timeout="${4:-30}";
    local end=$((SECONDS+timeout))
    while (( SECONDS < end )); do
      if { exec 3<>/dev/tcp/"$host"/"$port"; } 2>/dev/null; then
        printf 'POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}' "$path" "$host" >&3 || true
        if read -r -t 1 _line <&3; then
          exec 3>&- 3<&-
          return 0
        fi
        exec 3>&- 3<&-
      fi
      sleep 0.2
    done
    return 1
  }
  local STDIN_FIFO="/tmp/mcp_http_stdin.fifo"
  rm -f "$STDIN_FIFO" 2>/dev/null || true
  mkfifo "$STDIN_FIFO"
  ( tail -f /dev/null > "$STDIN_FIFO" ) &
  local HOLD_PID=$!
  "$SERVER_BIN" --transport=http --listen="$URL" 0<"$STDIN_FIFO" 2>&2 &
  local SERVER_PID=$!
  local hp="${URL#*://}"; local host="${hp%%:*}"; local port="${hp##*:}"
  sleep 0.6
  if ! waitForHttp "$host" "$port" /mcp/rpc 30; then kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1; fi

  # If Bearer auth is required, first verify unauthorized path yields 401 and WWW-Authenticate header
  if [[ "${MCP_HTTP_REQUIRE_BEARER:-0}" == "1" ]]; then
    echo "${C_INFO}[demo][http] Verifying unauthorized request => 401 + WWW-Authenticate${C_RST}" >&2
    # Send a minimal POST without Authorization and capture status + WWW-Authenticate header
    exec 5<>/dev/tcp/"$host"/"$port" || { echo "${C_ERR}[demo][http] Failed to open TCP for unauthorized probe${C_RST}" >&2; set +e; kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1; }
    printf 'POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}' "/mcp/rpc" "$host" >&5 || true
    local statusLine
    if ! IFS= read -r -t 2 statusLine <&5; then statusLine=""; fi
    # Read headers and look for WWW-Authenticate
    local wwwHeader="" line
    while IFS= read -r -t 2 line <&5; do
      # Strip CR if present
      line="${line%$'\r'}"
      [[ -z "$line" ]] && break
      case "$line" in
        WWW-Authenticate:*) wwwHeader="$line" ;;
      esac
    done
    exec 5>&- 5<&-
    local code
    code=$(awk '{print $2}' <<< "$statusLine")
    if [[ "$code" != "401" ]]; then
      echo "${C_ERR}[demo][http] Expected 401, got: '$statusLine'${C_RST}" >&2
      set +e; kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1
    fi
    if [[ -z "$wwwHeader" || "$wwwHeader" != *"Bearer"* || "$wwwHeader" != *"resource_metadata="* ]]; then
      echo "${C_ERR}[demo][http] Missing/invalid WWW-Authenticate header: '$wwwHeader'${C_RST}" >&2
      set +e; kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1
    fi
    echo "${C_OK}[demo][http] Unauthorized probe OK (401 + WWW-Authenticate)${C_RST}" >&2

    # Also verify notify-path unauthorized => 401 + header
    echo "${C_INFO}[demo][http] Verifying unauthorized notify => 401 + WWW-Authenticate${C_RST}" >&2
    exec 5<>/dev/tcp/"$host"/"$port" || { echo "${C_ERR}[demo][http] Failed to open TCP for unauthorized notify probe${C_RST}" >&2; set +e; kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1; }
    printf 'POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: 23\r\nConnection: close\r\n\r\n{"jsonrpc":"2.0","method":"n"}' "/mcp/notify" "$host" >&5 || true
    if ! IFS= read -r -t 2 statusLine <&5; then statusLine=""; fi
    wwwHeader=""; while IFS= read -r -t 2 line <&5; do line="${line%$'\r'}"; [[ -z "$line" ]] && break; case "$line" in WWW-Authenticate:*) wwwHeader="$line";; esac; done
    exec 5>&- 5<&-
    code=$(awk '{print $2}' <<< "$statusLine")
    if [[ "$code" != "401" ]]; then echo "${C_ERR}[demo][http] Expected 401 for notify, got: '$statusLine'${C_RST}" >&2; set +e; kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1; fi
    if [[ -z "$wwwHeader" || "$wwwHeader" != *"Bearer"* || "$wwwHeader" != *"resource_metadata="* ]]; then echo "${C_ERR}[demo][http] Missing/invalid WWW-Authenticate header on notify: '$wwwHeader'${C_RST}" >&2; set +e; kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1; fi
    echo "${C_OK}[demo][http] Unauthorized notify OK (401 + WWW-Authenticate)${C_RST}" >&2
  fi

  set +e
  # Authorized client: add bearer header if required
  if [[ "${MCP_HTTP_REQUIRE_BEARER:-0}" == "1" ]]; then
    local token="${MCP_HTTP_DEMO_TOKEN:-demo}"
    "$CLIENT_BIN" --transport=http --url="$URL" --httpcfg="auth=bearer; bearerToken=${token}" 2>&2
    # Authorized notify path using same token => 200
    exec 5<>/dev/tcp/"$host"/"$port" || { echo "${C_ERR}[demo][http] Failed to open TCP for authorized notify${C_RST}" >&2; set +e; kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1; }
    printf 'POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nAuthorization: Bearer %s\r\nContent-Length: 23\r\nConnection: close\r\n\r\n{"jsonrpc":"2.0","method":"n"}' "/mcp/notify" "$host" "$token" >&5 || true
    if ! IFS= read -r -t 2 statusLine <&5; then statusLine=""; fi
    exec 5>&- 5<&-
    code=$(awk '{print $2}' <<< "$statusLine")
    if [[ "$code" != "200" ]]; then echo "${C_ERR}[demo][http] Expected 200 for authorized notify, got: '$statusLine'${C_RST}" >&2; set +e; kill "$HOLD_PID" 2>/dev/null || true; rm -f "$STDIN_FIFO" 2>/dev/null || true; kill -TERM "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; return 1; fi
  else
    "$CLIENT_BIN" --transport=http --url="$URL" 2>&2
  fi
  local CLIENT_STATUS=$?
  set -e
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$HOLD_PID" 2>/dev/null || true
    for i in {1..10}; do
      if ! kill -0 "$SERVER_PID" 2>/dev/null; then break; fi
      sleep 0.1
    done
    if kill -0 "$SERVER_PID" 2>/dev/null; then
      kill -TERM "$SERVER_PID" 2>/dev/null || true
      sleep 0.2
      if kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -KILL "$SERVER_PID" 2>/dev/null || true
      fi
    fi
    wait "$SERVER_PID" 2>/dev/null || true
    rm -f "$STDIN_FIFO" 2>/dev/null || true
  fi
  if [[ "$CLIENT_STATUS" -eq 0 ]]; then
    echo "${C_OK}[demo][http] SUCCESS${C_RST}" >&2
    return 0
  else
    echo "${C_ERR}[demo][http] FAILURE (client exit $CLIENT_STATUS)${C_RST}" >&2
    return 1
  fi
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  overall=0
  run_stdio_demo || overall=1
  run_shm_demo   || overall=1
  run_http_demo  || overall=1
  if [[ "$overall" -eq 0 ]]; then
    echo "${C_OK}[demo] All transport demos succeeded${C_RST}" >&2
  else
    echo "${C_ERR}[demo] One or more transport demos failed${C_RST}" >&2
  fi
  exit "$overall"
fi
