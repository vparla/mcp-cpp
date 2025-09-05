#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: run_demo.sh
# Purpose: Demo runner: wires mcp_server and mcp_client over stdio using FIFOs
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
C2S="/tmp/mcp_c2s.fifo" # client stdout -> server stdin
S2C="/tmp/mcp_s2c.fifo" # server stdout -> client stdin

cleanup() {
  set +e
  if [[ -n "${SERVER_PID:-}" ]]; then kill "${SERVER_PID}" 2>/dev/null || true; fi
  rm -f "$C2S" "$S2C" 2>/dev/null || true
}
trap cleanup EXIT

mkfifo "$C2S" "$S2C"

# Pre-open both FIFOs RDWR to avoid open() blocking on the first writer/reader
exec 3<>"$C2S"
exec 4<>"$S2C"

# Start server: stdin from FD 3 (C2S), stdout to FD 4 (S2C)
# Close inherited 3 and 4 in the child to avoid keeping FIFOs open
"$SERVER_BIN" 0<&3 1>&4 2>&2 3<&- 4<&- &
SERVER_PID=$!

# Small delay to let server initialize
sleep 0.1

# Optionally demonstrate reload (send SIGHUP) before running client
if [[ "${DEMO_SEND_HUP:-0}" == "1" ]]; then
  kill -HUP "$SERVER_PID" 2>/dev/null || true
  sleep 0.1
fi

# Run client: stdin from FD 4 (S2C), stdout to FD 3 (C2S)
# Close inherited 3 and 4 in the child to avoid keeping FIFOs open
"$CLIENT_BIN" 0<&4 1>&3 2>&2 3<&- 4<&-
CLIENT_STATUS=$?

# Close extra RDWR descriptors to allow EOF propagation on FIFOs
echo "${C_INFO}[demo] Closing FIFO descriptors${C_RST}" >&2
exec 3>&- || true
exec 4>&- || true

# Gracefully stop server via SIGTERM and wait up to 5s, then SIGKILL if needed
echo "${C_INFO}[demo] Sending SIGTERM to server (pid=$SERVER_PID)${C_RST}" >&2
kill -TERM "$SERVER_PID" 2>/dev/null || true

# Poll for exit (50 * 100ms = 5s)
for i in {1..50}; do
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "${C_OK}[demo] Server exited after SIGTERM${C_RST}" >&2
    break
  fi
  sleep 0.1
done

if kill -0 "$SERVER_PID" 2>/dev/null; then
  echo "${C_WARN}[demo][WARN] Server didn't exit after SIGTERM; sending SIGKILL${C_RST}" >&2
  kill -KILL "$SERVER_PID" 2>/dev/null || true
fi

wait "$SERVER_PID" 2>/dev/null || true

exit "$CLIENT_STATUS"
