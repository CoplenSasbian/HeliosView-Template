#!/usr/bin/env python3
"""
setup-dependencies.py

HeliosView-Template Dependency Setup - the Python twin of
scripts/setup-dependencies.ps1, with the same pipeline and the same flags.

Why both exist: HeliosView ships a PowerShell and a Python variant of its own
setup script, and its CMake FATAL_ERROR hints point at both
(`.\\scripts\\setup-dependencies.ps1` / `python scripts/setup-dependencies.py`).
Having both here keeps those hints true at the template level, and the Python
route is the one to use behind a proxy (--proxy), where the PowerShell variant
has no equivalent switch.

Pipeline:
  [1/4] Git submodule HeliosView
  [2/4] HeliosView's own dependencies (delegated to its setup-dependencies.py:
        stdexec, Boost superproject + libraries, blend2d, asmjit, OpenSSL,
        cacert.pem, WebView2 SDK)
  [3/4] Frontend (Vite) dependencies - npm install
  [4/4] Verification

Idempotent, and run automatically by scripts/dev.cmd / build.cmd when
something is missing.

Usage:
  python scripts/setup-dependencies.py
  python scripts/setup-dependencies.py --force
  python scripts/setup-dependencies.py --skip-submodules
  python scripts/setup-dependencies.py --skip-downloads
  python scripts/setup-dependencies.py --skip-frontend
  python scripts/setup-dependencies.py --proxy http://127.0.0.1:7890
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# The exact artifacts HeliosView's CMake refuses to configure without.
# Mirrors scripts/_deps.cmd and scripts/setup-dependencies.ps1, and
# HeliosView's cmake/ensure-submodule.cmake + CMakeLists.txt.
DEPENDENCY_CHECKS = [
    ("HeliosView submodule", "HeliosView/.git"),
    ("stdexec", "HeliosView/third_party/stdexec/.git"),
    ("Boost superproject", "HeliosView/third_party/boost/.git"),
    ("Boost libraries", "HeliosView/third_party/boost/libs/json/.git"),
    ("blend2d", "HeliosView/third_party/blend2d/.git"),
    ("asmjit", "HeliosView/third_party/asmjit/.git"),
    ("OpenSSL", "HeliosView/third_party/openssl/build/native/include/openssl/ssl.h"),
    ("cacert.pem", "HeliosView/third_party/cacert.pem"),
    ("WebView2 SDK", "HeliosView/third_party/webview2-sdk/build/native/include/WebView2.h"),
]


def run(cmd, cwd=None):
    """Runs a command, streaming its output; returns the exit code."""
    print("  $ " + " ".join(str(c) for c in cmd), flush=True)
    try:
        return subprocess.call([str(c) for c in cmd], cwd=str(cwd) if cwd else None)
    except FileNotFoundError as exc:
        print(f"  ERROR: {exc}", file=sys.stderr)
        return 1


def missing_dependencies():
    return [name for name, marker in DEPENDENCY_CHECKS if not (REPO_ROOT / marker).exists()]


def step1_heliosview_submodule(args):
    """[1/4] The template's own dependency: the HeliosView submodule."""
    if args.force or not (REPO_ROOT / "HeliosView" / ".git").exists():
        print("\n[1/4] Initializing HeliosView submodule...")
        cmd = ["git"]
        if args.proxy:
            # Per-invocation (-c) instead of writing to the user's global config.
            cmd += ["-c", f"http.proxy={args.proxy}", "-c", f"https.proxy={args.proxy}"]
        cmd += ["submodule", "update", "--init", "--", "HeliosView"]
        if run(cmd, cwd=REPO_ROOT) != 0:
            print("ERROR: failed to initialize the HeliosView submodule.", file=sys.stderr)
            return False
    else:
        print("\n[1/4] HeliosView submodule is already initialized.")
    return True


def step2_heliosview_dependencies(args):
    """[2/4] Delegate: HeliosView owns its dependency list."""
    helios_py = REPO_ROOT / "HeliosView" / "scripts" / "setup-dependencies.py"
    helios_ps1 = REPO_ROOT / "HeliosView" / "scripts" / "setup-dependencies.ps1"

    print("\n[2/4] Running HeliosView's own dependency setup...")

    if helios_py.exists():
        cmd = [sys.executable, helios_py]
        if args.force:
            cmd.append("--force")
        if args.skip_submodules:
            cmd.append("--skip-submodules")
        if args.skip_downloads:
            cmd.append("--skip-downloads")
        if args.proxy:
            cmd += ["--proxy", args.proxy]
        cmd += ["--jobs", str(args.jobs)]
        rc = run(cmd, cwd=REPO_ROOT / "HeliosView")
    elif helios_ps1.exists():
        # Only reachable on a checkout that predates HeliosView's Python script.
        cmd = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", helios_ps1]
        if args.force:
            cmd.append("-Force")
        if args.skip_submodules:
            cmd.append("-SkipSubmodules")
        if args.skip_downloads:
            cmd.append("-SkipDownloads")
        cmd += ["-Jobs", str(args.jobs)]
        rc = run(cmd, cwd=REPO_ROOT / "HeliosView")
    else:
        print(f"ERROR: HeliosView setup script not found in {helios_py.parent}", file=sys.stderr)
        return False

    if rc != 0:
        print("ERROR: HeliosView dependency setup failed.", file=sys.stderr)
        return False
    return True


def step3_frontend(args):
    """[3/4] The frontend is a plain npm project - not the library's business."""
    frontend = REPO_ROOT / "frontend"
    package_json = frontend / "package.json"

    if args.skip_frontend:
        print("\n[3/4] Skipping frontend dependencies (--skip-frontend specified).")
        return True
    if not package_json.exists():
        print("\n[3/4] No frontend/package.json - skipping frontend dependencies.")
        print("      Scaffold one first: scripts\\setup.cmd")
        return True
    # `npm install` writes node_modules/.package-lock.json when it finishes, so
    # a bare node_modules/ directory is not proof of a complete install (an
    # aborted install leaves one behind). Probe the lockfile, not the directory.
    if (frontend / "node_modules" / ".package-lock.json").exists() and not args.force:
        print("\n[3/4] Frontend dependencies are already installed.")
        return True

    print("\n[3/4] Installing frontend dependencies (npm install)...")
    npm = shutil.which("npm")
    if not npm:
        print("ERROR: npm not found - Node.js is required (https://nodejs.org).", file=sys.stderr)
        return False
    if run([npm, "install"], cwd=frontend) != 0:
        print("ERROR: npm install failed.", file=sys.stderr)
        return False
    return True


def step4_verify():
    """[4/4] The contract the CMake configure step depends on."""
    print("\n[4/4] Verifying dependencies...")
    missing = set(missing_dependencies())
    for name, marker in DEPENDENCY_CHECKS:
        if name in missing:
            print(f"  [MISSING] {name:<22} {marker}")
        else:
            print(f"  [OK]      {name:<22} {marker}")
    if missing:
        print(
            "\nERROR: dependencies are incomplete - missing: "
            + ", ".join(sorted(missing))
            + "\nRe-run with --force, or see the log above for the failing step.",
            file=sys.stderr,
        )
        return False
    return True


def main():
    parser = argparse.ArgumentParser(
        description="HeliosView-Template Dependency Setup (Python implementation)"
    )
    parser.add_argument("--force", action="store_true", help="Force re-download and re-extraction")
    parser.add_argument("--skip-submodules", action="store_true", help="Skip git submodule updates")
    parser.add_argument("--skip-downloads", action="store_true", help="Skip binary downloads")
    parser.add_argument("--skip-frontend", action="store_true", help="Skip npm install")
    parser.add_argument("--proxy", type=str, default=None,
                        help="HTTP/HTTPS proxy URL (e.g. http://127.0.0.1:7890)")
    parser.add_argument("--jobs", type=int, default=4, help="Number of parallel git jobs")
    args = parser.parse_args()

    os.chdir(REPO_ROOT)
    print("=" * 58)
    print(" HeliosView-Template Dependency Setup")
    print(f" Working directory: {REPO_ROOT}")
    print("=" * 58)

    for step in (lambda: step1_heliosview_submodule(args),
                 lambda: step2_heliosview_dependencies(args),
                 lambda: step3_frontend(args),
                 step4_verify):
        if not step():
            return 1

    print("\n" + "=" * 58)
    print(" Dependencies setup complete! You can now configure CMake.")
    print("   Dev loop :  scripts\\dev.cmd")
    print("   Release  :  scripts\\build.cmd")
    print("=" * 58)
    return 0


if __name__ == "__main__":
    sys.exit(main())
