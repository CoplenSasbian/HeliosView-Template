# =====================================================================
# setup-dependencies.ps1
#
# HeliosView-Template Dependency Setup - THE single entry point.
#
# The template's own dependency is one: the HeliosView submodule. But
# HeliosView is not standalone - it needs its own third_party tree, which it
# deliberately does NOT fetch at CMake configure time (its CMake hard-fails
# with "run the setup script"). So a complete setup is a two-level pipeline,
# plus the frontend:
#
#   [1/4] Git submodule HeliosView
#   [2/4] HeliosView's own dependencies - delegated to its own
#         scripts/setup-dependencies.ps1, which fetches:
#           - git submodules: stdexec, the Boost superproject + the ~30 Boost
#             libraries HeliosView uses, blend2d, asmjit
#           - OpenSSL 3.5.2 (vcpkg NuGet)  -> HeliosView/third_party/openssl
#           - CA bundle (cacert.pem)       -> HeliosView/third_party/cacert.pem
#           - WebView2 SDK (NuGet)         -> HeliosView/third_party/webview2-sdk
#   [3/4] Frontend (Vite) dependencies - npm install
#   [4/4] Verification - every marker HeliosView's CMake hard-fails on
#
# Steps 1 and 4 are template-level; step 2 is delegated because HeliosView
# owns its dependency list (never duplicate it here); step 3 belongs to the
# frontend, which the library knows nothing about.
#
# Idempotent: every step probes first and skips what is already there, so
# running it again after a partial failure just finishes the missing parts.
# scripts\dev.cmd / build.cmd run it automatically when something is
# missing (see _deps.cmd) - you normally never call it by hand.
#
# Usage:
#   .\scripts\setup-dependencies.ps1
#   .\scripts\setup-dependencies.ps1 -Force              # re-fetch everything
#   .\scripts\setup-dependencies.ps1 -SkipSubmodules     # no git submodules
#   .\scripts\setup-dependencies.ps1 -SkipDownloads      # no NuGet/CA downloads
#   .\scripts\setup-dependencies.ps1 -SkipFrontend       # no npm install
#   .\scripts\setup-dependencies.ps1 -Proxy http://127.0.0.1:7890
# =====================================================================

param(
    [switch]$Force,
    [switch]$SkipSubmodules,
    [switch]$SkipDownloads,
    [switch]$SkipFrontend,
    [string]$Proxy,
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host " HeliosView-Template Dependency Setup" -ForegroundColor Cyan
Write-Host " Working directory: $RepoRoot" -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# ---------------------------------------------------------------------
# Native-command helper.
#
# Windows PowerShell turns a native command's stderr into a terminating error
# when $ErrorActionPreference is 'Stop' - and git/npm write clone progress and
# warnings there, so a plain call would abort the setup mid-download. This
# wrapper relaxes the preference for the duration of the call (the preference
# variable is scoped, so the caller is unaffected), passes the output through
# untouched - a silent multi-minute clone looks like a hang - and returns the
# exit code for the caller to check.
# ---------------------------------------------------------------------
function Invoke-Native {
    param([string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory)
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($WorkingDirectory) { Push-Location $WorkingDirectory }
        & $FilePath @Arguments
        $code = $LASTEXITCODE
    } finally {
        if ($WorkingDirectory) { Pop-Location }
        $ErrorActionPreference = $prevEap
    }
    return $code
}

# ---------------------------------------------------------------------
# Dependency markers - the exact artifacts HeliosView's CMake refuses to
# configure without. Mirrors scripts/_deps.cmd and HeliosView's
# cmake/ensure-submodule.cmake + CMakeLists.txt. Step [4/4] checks these.
# ---------------------------------------------------------------------
$DependencyChecks = [ordered]@{
    'HeliosView submodule' = 'HeliosView/.git'
    'stdexec'              = 'HeliosView/third_party/stdexec/.git'
    'Boost superproject'   = 'HeliosView/third_party/boost/.git'
    'Boost libraries'      = 'HeliosView/third_party/boost/libs/json/.git'
    'blend2d'              = 'HeliosView/third_party/blend2d/.git'
    'asmjit'               = 'HeliosView/third_party/asmjit/.git'
    'OpenSSL'              = 'HeliosView/third_party/openssl/build/native/include/openssl/ssl.h'
    'cacert.pem'           = 'HeliosView/third_party/cacert.pem'
    'WebView2 SDK'         = 'HeliosView/third_party/webview2-sdk/build/native/include/WebView2.h'
}

function Get-MissingDependencies {
    $missing = @()
    foreach ($name in $DependencyChecks.Keys) {
        if (-not (Test-Path (Join-Path $RepoRoot $DependencyChecks[$name]))) {
            $missing += $name
        }
    }
    return $missing
}

# ---------------------------------------------------------------------
# 1. Initialize the HeliosView submodule (the template's only own dependency)
# ---------------------------------------------------------------------
$heliosViewDir = Join-Path $RepoRoot 'HeliosView'
if ($Force -or (-not (Test-Path (Join-Path $heliosViewDir '.git')))) {
    Write-Host "`n[1/4] Initializing HeliosView submodule..." -ForegroundColor Cyan
    $gitArgs = @()
    if ($Proxy) {
        # Configure the proxy per-invocation (-c) instead of writing it into
        # the user's global git config.
        $gitArgs += @('-c', "http.proxy=$Proxy", '-c', "https.proxy=$Proxy")
    }
    $gitArgs += @('submodule', 'update', '--init', '--', 'HeliosView')
    if ((Invoke-Native -FilePath 'git' -Arguments $gitArgs) -ne 0) {
        Write-Error "Failed to initialize HeliosView submodule."
        exit 1
    }
} else {
    Write-Host "`n[1/4] HeliosView submodule is already initialized." -ForegroundColor Green
}

# ---------------------------------------------------------------------
# 2. Delegate to HeliosView's own setup script - it owns its dependency
#    list (submodules + OpenSSL + cacert.pem + WebView2 SDK).
# ---------------------------------------------------------------------
$heliosPs1 = Join-Path $heliosViewDir 'scripts/setup-dependencies.ps1'
$heliosPy  = Join-Path $heliosViewDir 'scripts/setup-dependencies.py'

Write-Host "`n[2/4] Running HeliosView's own dependency setup..." -ForegroundColor Cyan

if ($Proxy) {
    # HeliosView's PowerShell variant has no proxy switch; its Python variant
    # does (and also honours HTTP_PROXY/HTTPS_PROXY). Prefer Python whenever a
    # proxy is requested, so -Proxy works end to end.
    if (-not (Test-Path $heliosPy)) {
        Write-Error "HeliosView Python setup script not found at: $heliosPy (needed for -Proxy)."
        exit 1
    }
    $pythonExe = $null
    foreach ($candidate in @('python', 'python3', 'py')) {
        $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($cmd) { $pythonExe = $cmd.Source; break }
    }
    if (-not $pythonExe) {
        Write-Error "Python was not found on PATH (needed for -Proxy). Install Python 3, or run without -Proxy."
        exit 1
    }
    Write-Host "  Using proxy: $Proxy (via $pythonExe)" -ForegroundColor Yellow
    $pyArgs = @($heliosPy)
    if ($Force)          { $pyArgs += '--force' }
    if ($SkipSubmodules) { $pyArgs += '--skip-submodules' }
    if ($SkipDownloads)  { $pyArgs += '--skip-downloads' }
    $pyArgs += @('--proxy', $Proxy, '--jobs', "$Jobs")
    $rc = Invoke-Native -FilePath $pythonExe -Arguments $pyArgs
    if ($rc -ne 0) {
        Write-Error "HeliosView dependency setup script failed."
        exit 1
    }
} else {
    if (-not (Test-Path $heliosPs1)) {
        Write-Error "HeliosView setup script not found at: $heliosPs1"
        exit 1
    }
    $scriptParams = @{}
    if ($Force)          { $scriptParams['Force'] = $true }
    if ($SkipSubmodules) { $scriptParams['SkipSubmodules'] = $true }
    if ($SkipDownloads)  { $scriptParams['SkipDownloads'] = $true }
    if ($Jobs -gt 0)     { $scriptParams['Jobs'] = $Jobs }

    try {
        & $heliosPs1 @scriptParams
    } catch {
        Write-Error "HeliosView dependency setup script failed: $($_.Exception.Message)"
        exit 1
    }
    if (-not $?) {
        Write-Error "HeliosView dependency setup script failed."
        exit 1
    }
}
Set-Location $RepoRoot   # the delegated script moves the session's cwd

# ---------------------------------------------------------------------
# 3. Frontend (Vite) dependencies
# ---------------------------------------------------------------------
$frontendDir = Join-Path $RepoRoot 'frontend'
$frontendPkg = Join-Path $frontendDir 'package.json'
# `npm install` writes node_modules/.package-lock.json when it finishes, so a
# bare node_modules/ directory is not proof of a complete install (an aborted
# install leaves one behind). Probe the lockfile, not the directory.
$frontendInstalled = Test-Path (Join-Path $frontendDir 'node_modules/.package-lock.json')

if ($SkipFrontend) {
    Write-Host "`n[3/4] Skipping frontend dependencies (-SkipFrontend specified)." -ForegroundColor DarkGray
} elseif (-not (Test-Path $frontendPkg)) {
    Write-Host "`n[3/4] No frontend/package.json - skipping frontend dependencies." -ForegroundColor DarkGray
    Write-Host "      Scaffold one first: scripts\setup.cmd" -ForegroundColor DarkGray
} elseif ($frontendInstalled -and (-not $Force)) {
    Write-Host "`n[3/4] Frontend dependencies are already installed." -ForegroundColor Green
} else {
    Write-Host "`n[3/4] Installing frontend dependencies (npm install)..." -ForegroundColor Cyan
    if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
        Write-Error "npm not found - Node.js is required for the frontend (https://nodejs.org)."
        exit 1
    }
    if ((Invoke-Native -FilePath 'npm' -Arguments @('install') -WorkingDirectory $frontendDir) -ne 0) {
        Write-Error "npm install failed."
        exit 1
    }
}

# ---------------------------------------------------------------------
# 4. Verification - the contract the CMake configure step depends on
# ---------------------------------------------------------------------
Write-Host "`n[4/4] Verifying dependencies..." -ForegroundColor Cyan
$missing = Get-MissingDependencies
foreach ($name in $DependencyChecks.Keys) {
    $marker = $DependencyChecks[$name]
    if ($missing -contains $name) {
        Write-Host ("  [MISSING] {0,-22} {1}" -f $name, $marker) -ForegroundColor Red
    } else {
        Write-Host ("  [OK]      {0,-22} {1}" -f $name, $marker) -ForegroundColor Green
    }
}

if ($missing.Count -gt 0) {
    Write-Host ""
    Write-Error ("Dependencies are incomplete - missing: " + ($missing -join ', ') + "`nRe-run with -Force, or see the log above for the failing step.")
    exit 1
}

Write-Host "`n==========================================================" -ForegroundColor Green
Write-Host " Dependencies setup complete! You can now configure CMake." -ForegroundColor Green
Write-Host "   Dev loop :  scripts\dev.cmd" -ForegroundColor Green
Write-Host "   Release  :  scripts\build.cmd" -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
