#!/usr/bin/env bash
set -euo pipefail

# Tear down the HTTP(S) two-node e2e environment
# Usage: ./scripts/http_down.sh

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root/tests/http"

echo "[http_down] Stopping compose..."
docker compose down -v --remove-orphans
