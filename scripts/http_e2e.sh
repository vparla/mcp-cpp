#!/usr/bin/env bash
set -euo pipefail

# Run the HTTP(S) two-node e2e: server + client tests
# Usage: ./scripts/http_e2e.sh

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root/tests/http"

echo "[http_e2e] Starting compose (server + client)..."
docker compose up --abort-on-container-exit --force-recreate --renew-anon-volumes --remove-orphans
status=$?

echo "[http_e2e] Compose finished with status=$status"
exit $status
