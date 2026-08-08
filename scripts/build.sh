#!/usr/bin/env bash
# build.sh — release build (macOS/Linux, or Git Bash on Windows; cmd
# users: build.cmd):
#   1. builds the frontend (vite build)  -> frontend/dist
#      (base: './' is set in vite.config.js, so the page works over file://)
#   2. builds the C++ app in prod mode, which copies dist -> exe-dir/assets
#
#   ./scripts/build.sh
#   Then run:  build/release/bin/HeliosViewApp

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/release"
FRONTEND="$ROOT/frontend"

fail() { echo "[build] ERROR: $*" >&2; exit 1; }

[[ -f "$FRONTEND/package.json" ]] || fail "frontend/ is not scaffolded yet. Run ./scripts/setup.sh first."
command -v cmake >/dev/null 2>&1 || fail "cmake is required (https://cmake.org)."
command -v ninja >/dev/null 2>&1 || fail "ninja is required on this platform (no Visual Studio generator)."

# ---- frontend --------------------------------------------------------------------
echo "[build] Building frontend (vite build)..."
(cd "$FRONTEND" && npm run build)

# ---- C++ app in prod mode ------------------------------------------------------------
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DHELIOSVIEW_TEMPLATE_DEV=OFF
cmake --build "$BUILD"

EXE="$BUILD/bin/HeliosViewApp"
[[ -f "$EXE.exe" ]] && EXE="$EXE.exe"   # Windows (Git Bash): the binary has a .exe suffix
echo ""
echo "[build] Done. Run the app:"
echo "    $EXE"
echo ""
echo "All runtime libraries and assets/ (the built frontend) sit next to the"
echo "exe - copy the whole bin/ folder to distribute."
