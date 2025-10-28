#!/usr/bin/env bash
set -euo pipefail

# All-in-one HTTP(S) e2e runner (server + client tests via Docker Compose)
# - Optional global cleanup: set CLEAN_ALL=1 to stop/remove ALL containers and prune networks/volumes
# - Otherwise, only the HTTP e2e stack and known test containers are removed
#
# Usage:
#   ./scripts/http_e2e_all.sh            # targeted cleanup + build + run
#   CLEAN_ALL=1 ./scripts/http_e2e_all.sh # global cleanup + build + run (destructive)

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

http_dir="$repo_root/tests/http"

log() { echo "[http_e2e_all] $*"; }

# 0) Cleanup
if [[ "${CLEAN_ALL:-0}" == "1" ]]; then
  log "GLOBAL CLEANUP: stopping/removing ALL containers and pruning networks/volumes (destructive)"
  docker ps -aq | xargs -r docker rm -f || true
  docker network prune -f || true
  docker volume prune -f || true
else
  log "Targeted cleanup: HTTP e2e stack and known test containers"
  (cd "$http_dir" && docker compose down -v --remove-orphans) || true
  # Remove any manually started containers from earlier debugging
  docker rm -f http-server http-client mcp-server mcp-client >/dev/null 2>&1 || true
fi

# 1) Ensure local base build image exists
log "Building base image 'mcp-cpp-build' (Dockerfile.demo --target build)"
docker buildx build \
  -f Dockerfile.demo \
  --target build \
  --progress=plain \
  --load \
  -t mcp-cpp-build \
  .

# 2) Build HTTP e2e image (server + client tests)
log "Building e2e image 'mcp-http' (tests/http/Dockerfile)"
docker build \
  -f tests/http/Dockerfile \
  --build-arg BUILD_IMAGE=mcp-cpp-build \
  -t mcp-http \
  .

# 3) Run compose (server+client), gate client on server health
log "Starting compose (server + client)"
(cd "$http_dir" && docker compose up --abort-on-container-exit --force-recreate --renew-anon-volumes --remove-orphans)
status=$?

log "Compose finished with status=$status"
exit $status
