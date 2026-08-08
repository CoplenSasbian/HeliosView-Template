#!/usr/bin/env bash
# vite.sh — run the Vite dev server for the frontend (dev mode, foreground;
# Ctrl+C stops it). Use with CLion: dev mode is the CMake default, so just run
# this script in a terminal, then build/run the app from CLion — it loads
# http://localhost:5173 (HMR).
#
#   ./scripts/vite.sh [port]          (default 5173)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-5173}"
FRONTEND="$ROOT/frontend"

fail() { echo "[vite] ERROR: $*" >&2; exit 1; }

[[ -f "$FRONTEND/package.json" ]] || fail "frontend/ is not scaffolded yet. Run ./scripts/setup.sh first."
command -v node >/dev/null 2>&1 || fail "Node.js is required (https://nodejs.org)."

if [[ ! -d "$FRONTEND/node_modules" ]]; then
    echo "[vite] Installing frontend dependencies..."
    (cd "$FRONTEND" && npm install)
fi

echo "[vite] Vite dev server on http://localhost:$PORT (HMR) - Ctrl+C to stop."
(cd "$FRONTEND" && npm run dev -- --port "$PORT" --strictPort)
