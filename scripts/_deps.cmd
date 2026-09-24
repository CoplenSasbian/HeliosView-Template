@echo off
REM =====================================================================
REM _deps.cmd - internal pre-flight helper for build.cmd / dev.cmd.
REM
REM Not meant to be run directly; call it with `call "%~dp0_deps.cmd"`.
REM
REM It verifies that the HeliosView submodule AND everything HeliosView
REM itself depends on is in place, and runs scripts\setup-dependencies.cmd
REM exactly once if anything is missing. A green pre-flight here means the
REM CMake configure step in the caller cannot fail on a missing dependency
REM (HeliosView's CMake hard-fails on every one of these markers).
REM
REM The marker list is the template-side mirror of:
REM   HeliosView/cmake/ensure-submodule.cmake   (stdexec, boost, blend2d, asmjit)
REM   HeliosView/CMakeLists.txt                 (OpenSSL, cacert.pem, WebView2 SDK)
REM Keep it in sync with the checks in setup-dependencies.ps1 / .py.
REM
REM Exports:
REM   %HELIOSVIEW_DEPS_OK%   1 when every dependency is in place
REM Returns:
REM   errorlevel 0 = ready to build, 1 = setup failed or still incomplete
REM
REM No setlocal on purpose - %HELIOSVIEW_DEPS_OK% must reach the caller
REM (same contract as _toolchain.cmd).
REM =====================================================================

REM ROOT is normally set by the caller (dev.cmd / build.cmd). The fallback
REM below must NOT use a parenthesized block: cmd expands %CD% when it parses
REM the block, i.e. before pushd has run, which would yield the scripts\ dir.
if defined ROOT goto have_root
pushd "%~dp0.." >nul
set "ROOT=%CD%"
popd >nul
:have_root

set "HELIOSVIEW_DEPS_OK="

call :check_deps
if not defined DEPS_MISSING (
    set "HELIOSVIEW_DEPS_OK=1"
    exit /b 0
)

echo [deps] missing: %DEPS_MISSING%
echo [deps] running scripts\setup-dependencies.cmd ...
REM -SkipFrontend: this pre-flight guards the CMake configure step, so it only
REM cares about the C++ dependency tree. dev.cmd / build.cmd install the
REM frontend packages themselves (they know whether they need them).
call "%~dp0setup-dependencies.cmd" -SkipFrontend
if errorlevel 1 (
    echo [deps] ERROR: dependency setup failed. 1>&2
    exit /b 1
)

REM Re-check: a setup script that "succeeded" without producing the files
REM would otherwise surface as an obscure CMake FATAL_ERROR later on.
call :check_deps
if defined DEPS_MISSING (
    echo [deps] ERROR: dependencies still missing after setup: %DEPS_MISSING% 1>&2
    exit /b 1
)

set "HELIOSVIEW_DEPS_OK=1"
exit /b 0

REM ---- dependency markers ------------------------------------------------
:check_deps
set "DEPS_MISSING="
call :need "HeliosView\.git"                                                     "HeliosView submodule"
call :need "HeliosView\third_party\stdexec\.git"                                 "stdexec"
call :need "HeliosView\third_party\boost\.git"                                   "Boost superproject"
call :need "HeliosView\third_party\boost\libs\json\.git"                         "Boost libraries"
call :need "HeliosView\third_party\blend2d\.git"                                 "blend2d"
call :need "HeliosView\third_party\asmjit\.git"                                  "asmjit"
call :need "HeliosView\third_party\openssl\build\native\include\openssl\ssl.h"   "OpenSSL"
call :need "HeliosView\third_party\cacert.pem"                                   "cacert.pem"
call :need "HeliosView\third_party\webview2-sdk\build\native\include\WebView2.h" "WebView2 SDK"
exit /b 0

:need
if exist "%ROOT%\%~1" exit /b 0
set "DEPS_MISSING=%DEPS_MISSING% %~2"
exit /b 0
