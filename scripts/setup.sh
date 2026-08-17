#!/usr/bin/env bash
# setup.sh — scaffold the frontend with your framework of choice.
# (macOS/Linux, or Git Bash on Windows; cmd users: setup.cmd)
#
# The repo ships with a vanilla JS frontend in frontend/ (works out of the
# box); run this script to switch frameworks. Uses the official Vite templates
# (react /
# vue / svelte / solid / preact / lit / vanilla, each in JS or TS), so every
# scaffold stays up to date.
#
#   ./scripts/setup.sh                  # interactive menu
#   ./scripts/setup.sh -t vue-ts        # non-interactive
#   ./scripts/setup.sh -t react-ts -f   # replace the existing frontend/

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FRONTEND="$ROOT/frontend"

TEMPLATE=""
FORCE=0

fail() { echo "[setup] ERROR: $*" >&2; exit 1; }

usage() {
    echo "usage: setup.sh [-t <template>] [-f] [-h]"
    echo "  -t, --template <name>  Vite template, e.g. vue-ts (default: interactive menu)"
    echo "  -f, --force            replace the existing frontend/ directory"
    echo "  -h, --help             show this help"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--template) TEMPLATE="$2"; shift 2 ;;
        -f|--force)    FORCE=1; shift ;;
        -h|--help)     usage; exit 0 ;;
        *)             fail "unknown option: $1 (see -h)" ;;
    esac
done

# ---- prerequisites ---------------------------------------------------------
command -v node >/dev/null 2>&1 || fail "Node.js is required (https://nodejs.org). Verify with 'node -v'."
command -v npm  >/dev/null 2>&1 || fail "npm not found (it ships with Node.js)."

if [[ -f "$FRONTEND/package.json" ]]; then
    if [[ $FORCE -ne 1 ]]; then
        fail "frontend/ already exists (ships with a vanilla JS app). Re-scaffold it with -f (this deletes frontend/)."
    fi
    echo "[setup] -f: removing existing frontend/..."
    rm -rf "$FRONTEND"
fi

# ---- template selection ----------------------------------------------------
KEYS=(react-ts react vue-ts vue svelte-ts svelte solid-ts solid
       preact-ts preact lit-ts lit vanilla-ts vanilla)
NAMES=("React + TypeScript" "React (JavaScript)" "Vue + TypeScript" "Vue (JavaScript)"
       "Svelte + TypeScript" "Svelte (JavaScript)" "Solid + TypeScript" "Solid (JavaScript)"
       "Preact + TypeScript" "Preact (JavaScript)" "Lit + TypeScript" "Lit (JavaScript)"
       "Vanilla TS (no framework)" "Vanilla JS (no framework)")

if [[ -z "$TEMPLATE" ]]; then
    echo ""
    echo "Pick a frontend framework (Vite template):"
    for i in "${!KEYS[@]}"; do
        printf "  [%2d] %-12s %s\n" "$i" "${KEYS[$i]}" "${NAMES[$i]}"
    done
    read -r -p "Enter a number (default 0: react-ts): " choice
    if [[ -z "$choice" ]]; then choice=0; fi
    if ! [[ "$choice" =~ ^[0-9]+$ ]] || (( choice < 0 || choice >= ${#KEYS[@]} )); then
        fail "Invalid choice: $choice"
    fi
    TEMPLATE="${KEYS[$choice]}"
fi

# ---- scaffold ---------------------------------------------------------------
echo "[setup] Scaffolding frontend with template '$TEMPLATE'..."
(cd "$ROOT" && npm create --yes vite@latest frontend -- --template "$TEMPLATE")

echo "[setup] Installing dependencies..."
(cd "$FRONTEND" && npm install)

# ---- bridge typings (TypeScript templates only) -----------------------------
if [[ "$TEMPLATE" == *-ts ]]; then
    cp "$ROOT/scripts/helios.d.ts" "$FRONTEND/src/helios.d.ts"
    echo "[setup] Wrote frontend/src/helios.d.ts (bridge typings)."
fi

echo ""
echo "[setup] Done. Next steps:"
echo "  1. Dev loop :  ./scripts/dev.sh       (C++ app + Vite dev server, HMR)"
echo "  2. Release  :  ./scripts/build.sh     (C++ app + built frontend)"
echo ""
echo "  Frontend structure:  frontend/  (Vite project, output: frontend/dist)"
