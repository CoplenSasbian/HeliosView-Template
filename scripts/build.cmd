@echo off
setlocal
REM build.cmd - release build (Windows, pure cmd - no PowerShell involved):
REM   1. builds the frontend (vite build)  -> frontend/dist
REM      (base: './' is set in vite.config.js, so the page works over file://)
REM   2. builds the C++ app in prod mode, which copies dist -> exe-dir/assets
REM
REM The MSVC environment and cmake/ninja are set up automatically (see
REM _toolchain.cmd) - run this from any cmd, no Developer prompt needed.
REM
REM   scripts\build.cmd
REM   Then run:  build\release\bin\HeliosViewApp.exe

REM Anchor to the script's own directory first: %~dp0 can be a relative path
REM depending on how the script is invoked, so nothing below may rely on the
REM caller's working directory.
cd /d "%~dp0" >nul 2>&1
pushd ".." >nul
set "ROOT=%CD%"
popd >nul
set "BUILD=%ROOT%\build\release"
set "FRONTEND=%ROOT%\frontend"

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

echo.
echo [build] Done. Run the app:
if defined APP_EXE (
    echo     "%BUILD%\bin\%APP_EXE%"
) else (
    echo     "%BUILD%\bin\HeliosViewApp.exe"
)
echo.
echo HeliosView.dll, WebView2Loader.dll and assets/ ^(the built frontend^)
echo all sit next to the exe - copy the whole bin/ folder to distribute.
exit /b 0
