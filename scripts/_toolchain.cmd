@echo off
REM _toolchain.cmd - internal helper for build.cmd / dev.cmd. Not meant to be
REM run directly; call it with `call "%~dp0_toolchain.cmd"`. It sets up the
REM MSVC environment (vcvarsall) and locates cmake / ninja, exporting them as
REM %CMAKE% / %NINJA% (NINJA may be empty = the scripts fall back to the Visual
REM Studio generator).
REM
REM Tool lookup order (no machine-specific paths are hardcoded):
REM   cmake: %CMAKE_COMMAND% (optional override)      -> `where cmake` (PATH)
REM   ninja: %CMAKE_MAKE_PROGRAM% (optional override) -> `where ninja` (PATH)
REM If cmake is still not found the script fails with a message: install it
REM (https://cmake.org, v4.3+) and put it on PATH, or set CMAKE_COMMAND to the
REM full path of cmake.exe.
REM
REM NOTE: no setlocal here - the environment changes (vcvarsall) must reach the
REM caller. Do NOT use %RC% as a variable after calling this: vcvarsall sets RC
REM to the path of rc.exe, and clobbering it breaks CMake's RC compiler detection.

REM ---- 1. MSVC environment (skipped when already inside a Developer prompt) ----
where cl >nul 2>&1 && goto have_msvc

REM %ProgramFiles(x86)% must not appear literally inside a parenthesized block
REM (its parentheses unbalance the parser) - snapshot it into a variable first.
set "PF=%ProgramFiles%"
set "PF86=%ProgramFiles(x86)%"

set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%PF%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [toolchain] ERROR: vswhere not found. Install Visual Studio with the C++ workload. 1>&2
    exit /b 1
)
for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%V"
if not defined VS_INSTALL (
    echo [toolchain] ERROR: no Visual Studio with C++ tools found. 1>&2
    exit /b 1
)
set "VCVARSALL=%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARSALL%" (
    echo [toolchain] ERROR: vcvarsall.bat not found in %VS_INSTALL%. 1>&2
    exit /b 1
)
echo [toolchain] MSVC: %VS_INSTALL%
call "%VCVARSALL%" x64 >nul
if errorlevel 1 (
    echo [toolchain] ERROR: vcvarsall x64 failed. 1>&2
    exit /b 1
)
:have_msvc

REM ---- 2. cmake ------------------------------------------------------------------
set "CMAKE="
if defined CMAKE_COMMAND (
    set "CMAKE=%CMAKE_COMMAND%"
) else (
    where cmake >nul 2>&1 && set "CMAKE=cmake"
)
if not defined CMAKE (
    echo [toolchain] ERROR: cmake not found. Install it ^(https://cmake.org, v4.3+^) 1>&2
    echo           and add it to PATH, or set CMAKE_COMMAND to the full path of cmake.exe. 1>&2
    exit /b 1
)
echo [toolchain] cmake: %CMAKE%

REM ---- 3. ninja (optional - the scripts fall back to the VS generator) ------------
set "NINJA="
if defined CMAKE_MAKE_PROGRAM (
    set "NINJA=%CMAKE_MAKE_PROGRAM%"
) else (
    where ninja >nul 2>&1 && set "NINJA=ninja"
)
if defined NINJA echo [toolchain] ninja: %NINJA%

exit /b 0
