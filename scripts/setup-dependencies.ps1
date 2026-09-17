# =====================================================================
# setup-dependencies.ps1
#
# HeliosView-Template Dependency Setup Script
#
# Fetches all external dependencies outside of CMake configure:
#   1. Git submodule HeliosView
#   2. HeliosView's own dependencies via its setup-dependencies.ps1
#
# Usage:
#   .\scripts\setup-dependencies.ps1
#   .\scripts\setup-dependencies.ps1 -Force
#   .\scripts\setup-dependencies.ps1 -SkipSubmodules
#   .\scripts\setup-dependencies.ps1 -SkipDownloads
# =====================================================================

param(
    [switch]$Force,
    [switch]$SkipSubmodules,
    [switch]$SkipDownloads,
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
# 1. Initialize HeliosView submodule
# ---------------------------------------------------------------------
$heliosViewDir = "$RepoRoot/HeliosView"
if ($Force -or (-not (Test-Path "$heliosViewDir/.git"))) {
    Write-Host "`n[1/2] Initializing HeliosView submodule..." -ForegroundColor Cyan
    & git submodule update --init -- HeliosView
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to initialize HeliosView submodule."
        exit 1
    }
} else {
    Write-Host "`n[1/2] HeliosView submodule is already initialized." -ForegroundColor Green
}

# ---------------------------------------------------------------------
# 2. Invoke HeliosView's own setup script
# ---------------------------------------------------------------------
$heliosSetupScript = "$heliosViewDir/scripts/setup-dependencies.ps1"
if (-not (Test-Path $heliosSetupScript)) {
    Write-Error "HeliosView setup script not found at: $heliosSetupScript"
    exit 1
}

Write-Host "`n[2/2] Running HeliosView dependencies setup..." -ForegroundColor Cyan
$scriptParams = @{}
if ($Force) { $scriptParams['Force'] = $true }
if ($SkipSubmodules) { $scriptParams['SkipSubmodules'] = $true }
if ($SkipDownloads) { $scriptParams['SkipDownloads'] = $true }
if ($Jobs -gt 0) { $scriptParams['Jobs'] = $Jobs }

& $heliosSetupScript @scriptParams
if (-not $?) {
    Write-Error "HeliosView dependency setup script failed."
    exit 1
}

Write-Host "`n==========================================================" -ForegroundColor Green
Write-Host " Dependencies setup complete! You can now configure CMake." -ForegroundColor Green
Write-Host " Example: cmake -B build -S . " -ForegroundColor Green
Write-Host "==========================================================" -ForegroundColor Green
