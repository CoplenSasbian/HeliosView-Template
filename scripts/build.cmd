@echo off
setlocal
REM build.cmd - release build for distribution (Windows only for now):
REM   1. builds the frontend (vite build)          -> frontend/dist
REM   2. builds the C++ app in prod mode            -> dist\ (see below)
REM
REM The CMake build tree lives in build\release, but all runtime artifacts are
REM routed to <repo>\dist (see the top-level CMakeLists.txt): the exe, HeliosView
REM dll, WebView2/OpenSSL dlls and assets/ land in dist\bin, the plugin dlls in
REM dist\plugins, and settings/logs are created next to them at runtime. The
REM whole dist\ folder is self-contained and directly distributable.
REM
REM   scripts\build.cmd
REM   Then run:  dist\bin\HeliosViewApp.exe

REM Anchor to the script's own directory first.
cd /d "%~dp0" >nul 2>&1
pushd ".." >nul
set "ROOT=%CD%"
popd >nul
set "BUILD=%ROOT%\build\release"
set "FRONTEND=%ROOT%\frontend"
set "DIST=%ROOT%\dist"
REM Executable name - passed to CMake as HELIOSVIEW_TEMPLATE_APP_NAME
REM (change it here or override with -DHELIOSVIEW_TEMPLATE_APP_NAME=...).
set "APP_NAME=HeliosViewApp"

if not exist "%FRONTEND%\package.json" (
    echo [build] ERROR: frontend/ is not scaffolded yet. Run scripts\setup.cmd first. 1>&2
    exit /b 1
)

REM ---- toolchain: MSVC env + cmake/ninja discovery -------------------------------
call "%~dp0_toolchain.cmd"
if errorlevel 1 exit /b 1

REM ---- frontend -------------------------------------------------------------------
echo [build] Building frontend (vite build)...
pushd "%FRONTEND%"
call npm run build
set "FE_RC=%ERRORLEVEL%"
popd
if not "%FE_RC%"=="0" ( echo [build] ERROR: Frontend build failed. 1>&2 & exit /b 1 )

REM ---- OpenSSL: reuse an existing download instead of re-fetching 22 MB -----------
REM HeliosView downloads the openssl.vcpkg nuget package at configure time and
REM caches it under the build dir. When the target build dir is new (e.g. a fresh
REM clone or a build\release created after cmake-build-debug), seed the cache from
REM any other build dir that already has it - saves bandwidth and works offline.
if not exist "%BUILD%\openssl\build\native\include\openssl\ssl.h" (
    if exist "%ROOT%\cmake-build-debug\openssl\build\native\include\openssl\ssl.h" (
        echo [build] Reusing OpenSSL cache from cmake-build-debug
        xcopy /e /i /q /y "%ROOT%\cmake-build-debug\openssl" "%BUILD%\openssl" >nul
    ) else if exist "%ROOT%\cmake-build-release\openssl\build\native\include\openssl\ssl.h" (
        echo [build] Reusing OpenSSL cache from cmake-build-release
        xcopy /e /i /q /y "%ROOT%\cmake-build-release\openssl" "%BUILD%\openssl" >nul
    ) else if exist "%ROOT%\build\dev\openssl\build\native\include\openssl\ssl.h" (
        echo [build] Reusing OpenSSL cache from build\dev
        xcopy /e /i /q /y "%ROOT%\build\dev\openssl" "%BUILD%\openssl" >nul
    )
)

REM ---- configure + build the C++ app in prod mode ----------------------------------
if defined NINJA (
    "%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release -DHELIOSVIEW_TEMPLATE_DEV=OFF -DHELIOSVIEW_TEMPLATE_APP_NAME=%APP_NAME%
) else (
    "%CMAKE%" -S "%ROOT%" -B "%BUILD%" -A x64 -DHELIOSVIEW_TEMPLATE_DEV=OFF -DHELIOSVIEW_TEMPLATE_APP_NAME=%APP_NAME%
)
if errorlevel 1 ( echo [build] ERROR: CMake configure failed. 1>&2 & exit /b 1 )
"%CMAKE%" --build "%BUILD%" --config Release -j 8
if errorlevel 1 ( echo [build] ERROR: CMake build failed. 1>&2 & exit /b 1 )

echo.
echo [build] Done. Run the app:
echo     "%DIST%\bin\%APP_NAME%.exe"
echo.
echo The whole %DIST%\ folder is the distributable:
echo   %DIST%\bin       exe + HeliosView.dll + WebView2/OpenSSL dlls + assets
echo   %DIST%\plugins   plugin dlls ^(loaded at runtime^)
echo.
exit /b 0
