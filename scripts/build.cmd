@echo off
setlocal
REM build.cmd - release build for distribution (Windows, pure cmd - no PowerShell involved):
REM   1. builds the frontend (vite build)  -> frontend/dist
REM      (base: './' is set in vite.config.js, so the page works over file://)
REM   2. builds the C++ app in prod mode    -> build\release\bin
REM   3. stages a clean distributable       -> dist\  (runtime files only)
REM
REM The CMake build tree (build\release) also holds build internals (lib\,
REM CMakeFiles\, openssl\, webview2-sdk\, ...) that the app does not need at
REM runtime. Everything the app needs sits in bin\ - the exe, HeliosView.dll,
REM WebView2Loader.dll, the OpenSSL dlls, cacert.pem and assets\ - so build.cmd
REM copies just that into dist\; the whole dist\ folder is directly distributable.
REM
REM The MSVC environment and cmake/ninja are set up automatically (see
REM _toolchain.cmd) - run this from any cmd, no Developer prompt needed.
REM
REM   scripts\build.cmd
REM   Then run:  dist\bin\HeliosViewApp.exe

REM Anchor to the script's own directory first: %~dp0 can be a relative path
REM depending on how the script is invoked, so nothing below may rely on the
REM caller's working directory.
cd /d "%~dp0" >nul 2>&1
pushd ".." >nul
set "ROOT=%CD%"
popd >nul
set "BUILD=%ROOT%\build\release"
set "FRONTEND=%ROOT%\frontend"
set "DIST=%ROOT%\dist"

if not exist "%FRONTEND%\package.json" (
    echo [build] ERROR: frontend/ is not scaffolded yet. Run scripts\setup.cmd first. 1>&2
    exit /b 1
)

REM ---- toolchain: MSVC env + cmake/ninja discovery ----------------------------------
call "%~dp0_toolchain.cmd"
if errorlevel 1 exit /b 1

REM ---- frontend --------------------------------------------------------------------
echo [build] Building frontend (vite build)...
pushd "%FRONTEND%"
call npm run build
set "FE_RC=%ERRORLEVEL%"
popd
if not "%FE_RC%"=="0" ( echo [build] ERROR: Frontend build failed. 1>&2 & exit /b 1 )

REM ---- C++ app in prod mode ------------------------------------------------------------
if defined NINJA (
    "%CMAKE%" -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_BUILD_TYPE=Release -DHELIOSVIEW_TEMPLATE_DEV=OFF
) else (
    "%CMAKE%" -S "%ROOT%" -B "%BUILD%" -A x64 -DHELIOSVIEW_TEMPLATE_DEV=OFF
)
if errorlevel 1 ( echo [build] ERROR: CMake configure failed. 1>&2 & exit /b 1 )
"%CMAKE%" --build "%BUILD%" --config Release -j 8
if errorlevel 1 ( echo [build] ERROR: CMake build failed. 1>&2 & exit /b 1 )

REM The executable name comes from app-config.cmake; find it in the build
REM output (bin\ holds exactly one .exe - the app).
for /f "delims=" %%E in ('dir /b "%BUILD%\bin\*.exe" 2^>nul') do set "APP_EXE=%%E"

REM ---- stage dist\ (runtime files only) ----------------------------------------------
echo [build] Staging dist\ (runtime files only)...
if exist "%DIST%" rmdir /s /q "%DIST%"
xcopy /e /i /q /y "%BUILD%\bin" "%DIST%\bin" >nul
if errorlevel 1 ( echo [build] ERROR: staging dist\ failed. 1>&2 & exit /b 1 )

echo.
echo [build] Done. Run the app:
if defined APP_EXE (
    echo     "%DIST%\bin\%APP_EXE%"
) else (
    echo     "%DIST%\bin\HeliosViewApp.exe"
)
echo.
echo The whole %DIST%\ folder is the distributable:
echo   %DIST%\bin       exe + HeliosView.dll + WebView2/OpenSSL dlls + assets\
echo.
exit /b 0
