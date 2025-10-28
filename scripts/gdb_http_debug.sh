#!/usr/bin/env bash
set -euo pipefail

# GDB-enabled rebuild + HTTP demo backtrace helper
# Usage:
#   ./scripts/gdb_http_debug.sh [--no-cache] [--url http://127.0.0.1:9443]
# Env overrides:
#   URL=http://127.0.0.1:9443  MCP_LOG_LEVEL=DEBUG

NO_CACHE=""
URL_DEFAULT="http://127.0.0.1:9443"
URL="${URL:-$URL_DEFAULT}"

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-cache)
      NO_CACHE="--no-cache"; shift ;;
    --url)
      URL="$2"; shift 2 ;;
    *) echo "Unknown arg: $1" >&2; exit 2 ;;
  esac
done

# 1) Rebuild compiled image with examples/tests
docker buildx build -f Dockerfile.demo --target build ${NO_CACHE} --progress=plain --load -t mcp-cpp-build .

# 2) Rebuild GDB-enabled image layering on top of compiled image
DOCKERFILE_DBG=$(cat <<'EOF'
FROM mcp-cpp-build
RUN apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends gdb \
 && rm -rf /var/lib/apt/lists/*
EOF
)
# Use classic docker build so it can see the locally-loaded base image
if [[ -n "${NO_CACHE}" ]]; then
  echo "${DOCKERFILE_DBG}" | docker build --no-cache -t mcp-cpp-build-dbg -f - .
else
  echo "${DOCKERFILE_DBG}" | docker build -t mcp-cpp-build-dbg -f - .
fi

# 3) Run HTTP-only GDB session to print backtrace on fault
# Pass URL and log level into the container for configurability
: "${MCP_LOG_LEVEL:=DEBUG}"

docker run --rm -i \
  --ipc=host \
  --cap-add=SYS_PTRACE --security-opt seccomp=unconfined \
  -e URL="${URL}" -e MCP_LOG_LEVEL="${MCP_LOG_LEVEL}" \
  mcp-cpp-build-dbg bash -s <<'CONTAINER'
set -euo pipefail

: "${URL:=http://127.0.0.1:9443}"

# Start server
/src/build/examples/mcp_server/mcp_server --transport=http --listen="${URL}" >/dev/null 2>&1 & spid=$!
sleep 0.6

# Run gdb in batch, capture output
GDB_LOG="/tmp/gdb.out"
gdb -q -batch -return-child-result \
  -ex "set pagination off" \
  -ex "handle SIGPIPE nostop noprint pass" \
  -ex "run" \
  -ex "bt full" \
  -ex "thread apply all bt full" \
  --args /src/build/examples/mcp_client/mcp_client --transport=http --url="${URL}" \
  2>&1 | tee "${GDB_LOG}"
gdb_status=${PIPESTATUS[0]}

# Determine exit code: prefer child result; mark nonzero if signal observed
code="$gdb_status"
if grep -q "Program received signal SIG" "${GDB_LOG}"; then
  code=139
fi

# Cleanup
kill -TERM $spid 2>/dev/null || true
wait $spid 2>/dev/null || true
exit "$code"
CONTAINER
