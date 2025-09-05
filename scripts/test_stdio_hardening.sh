#!/usr/bin/env bash
#==========================================================================================================
# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Vinny Parla
# File: test_stdio_hardening.sh
# Purpose: Integration tests for stdio transport hardening (idle read timeout, write queue overflow, write timeout)
#==========================================================================================================
set -euo pipefail

SCENARIO="${1:-}"
if [[ -z "$SCENARIO" ]]; then
  echo "usage: $0 <idle|overflow|writetimeout>" >&2
  exit 2
fi

SERVER_BIN="${SERVER_BIN:-/src/build/examples/mcp_server/mcp_server}"
C2S="/tmp/mcp_hardening_c2s.fifo" # client->server (server stdin)
S2C="/tmp/mcp_hardening_s2c.fifo" # server->client (server stdout)

cleanup() {
  set +e
  if [[ -n "${SERVER_PID:-}" ]]; then kill "${SERVER_PID}" 2>/dev/null || true; fi
  rm -f "$C2S" "$S2C" 2>/dev/null || true
}
trap cleanup EXIT

rm -f "$C2S" "$S2C" 2>/dev/null || true
mkfifo "$C2S" "$S2C"

# Pre-open both FIFOs RDWR so open() doesn't block, and to keep them open without an active peer.
exec 3<>"$C2S"
exec 4<>"$S2C"

# Build stdio config per scenario
CFG="timeout_ms=30000"
KA="" # keepalive override
case "$SCENARIO" in
  idle)
    CFG+=";idle_read_timeout_ms=200"
    ;;
  overflow)
    CFG+=";write_queue_max_bytes=4096" # small queue to overflow fast
    KA="MCP_KEEPALIVE_MS=1"            # spam keepalive to enqueue writes
    ;;
  writetimeout)
    CFG+=";write_timeout_ms=200;write_queue_max_bytes=10485760" # large queue, small write timeout
    KA="MCP_KEEPALIVE_MS=1"
    ;;
  badlength)
    CFG+=";idle_read_timeout_ms=0" # do not rely on idle timeout
    ;;
  *)
    echo "unknown scenario: $SCENARIO" >&2
    exit 2
    ;;
esac

export MCP_STDIO_CONFIG="$CFG"
export MCP_STDIO_MODE=1
if [[ -n "$KA" ]]; then export $KA; fi

# Launch server wired to FIFOs: stdin from FD 3 (C2S), stdout to FD 4 (S2C)
"$SERVER_BIN" 0<&3 1>&4 2>&2 3<&- 4<&- &
SERVER_PID=$!

# For overflow and write-timeout: do not read from S2C, just keep FD 4 open via exec so pipe fills
# For idle: do not write to C2S, so server sees no input; idle timer should fire.
# For badlength: write an invalid/oversized frame header then close write end to trigger parse error.
if [[ "$SCENARIO" == "badlength" ]]; then
  {
    printf 'Content-Length: 9999999999\r\n';
    printf '\r\n';
  } >&3 || true
  # Close writer to deliver EOF after header
  exec 3>&-
fi

# Wait for server to terminate within a reasonable timeout
TIMEOUT_SECS=5
START=$(date +%s)
while kill -0 "$SERVER_PID" 2>/dev/null; do
  NOW=$(date +%s)
  if (( NOW - START > TIMEOUT_SECS )); then
    echo "[FAIL] Server did not terminate for scenario=$SCENARIO within ${TIMEOUT_SECS}s" >&2
    exit 1
  fi
  sleep 0.1
done

# If we get here, the server exited (as expected for negative scenarios)
echo "[OK] Server terminated for scenario=$SCENARIO (expected)" >&2
exit 0
