#!/usr/bin/env bash
# dev.sh — the dev loop (macOS/Linux, or Git Bash on Windows; cmd
# users: dev.cmd):
#   1. starts the Vite dev server for the frontend (HMR)
#   2. builds and runs the C++ app, which loads the dev server URL
#
# Closing the app window (or Ctrl+C) stops the dev server too.
#
#   ./scripts/dev.sh [port]          (default 5173)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-5173}"
BUILD="$ROOT/build/dev"
FRONTEND="$ROOT/frontend"
LOG="$FRONTEND/.vite.log"
DEV_URL="http://localhost:$PORT"

fail() { echo "[dev] ERROR: $*" >&2; exit 1; }

[[ -f "$FRONTEND/package.json" ]] || fail "frontend/ is not scaffolded yet. Run ./scripts/setup.sh first."
command -v node  >/dev/null 2>&1 || fail "Node.js is required (https://nodejs.org)."
command -v cmake >/dev/null 2>&1 || fail "cmake is required (https://cmake.org)."
command -v ninja >/dev/null 2>&1 || fail "ninja is required on this platform (no Visual Studio generator)."

# ---- frontend dependencies --------------------------------------------------
if [[ ! -d "$FRONTEND/node_modules" ]]; then
    echo "[dev] Installing frontend dependencies..."
    (cd "$FRONTEND" && npm install)
fi

# ---- start the Vite dev server (background) ----------------------------------
(cd "$FRONTEND" && npm run dev -- --port "$PORT" --strictPort > "$LOG" 2>&1) &
VITE_PID=$!
echo "[dev] Vite dev server starting on port $PORT (log: frontend/.vite.log)..."

cleanup() {
    if kill -0 "$VITE_PID" 2>/dev/null; then
        kill "$VITE_PID" 2>/dev/null || true
        echo "[dev] Dev server stopped."
    fi
}
trap cleanup EXIT

# Wait until the server accepts connections (bounded; strictPort makes the URL stable)
ready=0
for _ in $(seq 1 120); do
    if ! kill -0 "$VITE_PID" 2>/dev/null; then fail "Vite exited early - see $LOG"; fi
    if node -e "fetch('$DEV_URL').then(r => process.exit(r.ok ? 0 : 1)).catch(() => process.exit(1))" 2>/dev/null; then
        ready=1
        break
    fi
    sleep 0.5
done
if [[ $ready -ne 1 ]]; then fail "Dev server did not become ready on $DEV_URL - see $LOG"; fi
echo "[dev] Dev server ready: $DEV_URL"

# ---- configure + build the C++ app in dev mode --------------------------------
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DHELIOSVIEW_TEMPLATE_DEV=ON -DHELIOSVIEW_TEMPLATE_DEV_URL="$DEV_URL"
cmake --build "$BUILD"

# ---- run ------------------------------------------------------------------------
EXE="$BUILD/bin/HeliosViewApp"
[[ -f "$EXE.exe" ]] && EXE="$EXE.exe"   # Windows (Git Bash): the binary has a .exe suffix
echo "[dev] Running $EXE (close the window to stop)..."
"$EXE"
