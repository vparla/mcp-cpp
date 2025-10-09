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
  "$SERVER_BIN" --transport=http --listen="$URL" 2>&2 &
  local SERVER_PID=$!
  sleep 0.4
  "$CLIENT_BIN" --transport=http --url="$URL" 2>&2
  local CLIENT_STATUS=$?
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    sleep 0.2
    kill -KILL "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [[ "$CLIENT_STATUS" -eq 0 ]]; then
    echo "${C_OK}[demo][http] SUCCESS${C_RST}" >&2
    return 0
  else
    echo "${C_ERR}[demo][http] FAILURE (client exit $CLIENT_STATUS)${C_RST}" >&2
    return 1
  fi
}

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
