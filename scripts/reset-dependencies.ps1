# =====================================================================
# reset-dependencies.ps1
#
# Cleans and resets all downloaded dependencies and third-party submodules.
# =====================================================================

param(
    [switch]$KeepDownloads
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$heliosResetScript = "$RepoRoot/HeliosView/scripts/reset-dependencies.ps1"
if (Test-Path $heliosResetScript) {
    $scriptArgs = @()
    if ($KeepDownloads) { $scriptArgs += "-KeepDownloads" }
    & $heliosResetScript @scriptArgs
} else {
    Write-Warning "HeliosView reset script not found: $heliosResetScript"
}
