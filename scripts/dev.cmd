@echo off
setlocal
REM dev.cmd - the dev loop for Windows (pure cmd - no PowerShell involved;
REM macOS/Linux: dev.sh):
REM   1. starts the Vite dev server for the frontend (HMR)
REM   2. builds and runs the C++ app, which loads the dev server URL
REM
REM Closing the app window (or Ctrl+C) stops the dev server too.
REM
REM   scripts\dev.cmd [-Port 5173]

set "PORT=5173"
set "RC=0"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-Port" (
    set "PORT=%~2"
    shift
    shift
    goto parse_args
)
echo [dev] ERROR: unknown option: %~1 1>&2
echo usage: dev.cmd [-Port 5173] 1>&2
exit /b 1
:args_done

REM Anchor to the script's own directory first: %~dp0 can be a relative path
REM depending on how the script is invoked, so nothing below may rely on the
REM caller's working directory.
cd /d "%~dp0" >nul 2>&1
pushd ".." >nul
set "ROOT=%CD%"
popd >nul

set "BUILD=%ROOT%\build\dev"
set "FRONTEND=%ROOT%\frontend"
set "LOGFILE=%FRONTEND%\.vite.log"
set "DEV_URL=http://localhost:%PORT%"

if not exist "%FRONTEND%\package.json" (
    echo [dev] ERROR: frontend/ is not scaffolded yet. Run scripts\setup.cmd first. 1>&2
    exit /b 1
)
where node  >nul 2>&1 || ( echo [dev] ERROR: Node.js is required ^(https://nodejs.org^). 1>&2 & exit /b 1 )
where cmake >nul 2>&1 || ( echo [dev] ERROR: cmake is required ^(https://cmake.org^). 1>&2 & exit /b 1 )
where curl  >nul 2>&1 || ( echo [dev] ERROR: curl is required ^(ships with Windows 10 1803+^). 1>&2 & exit /b 1 )

REM ---- frontend dependencies --------------------------------------------------
if not exist "%FRONTEND%\node_modules" (
    echo [dev] Installing frontend dependencies...
    pushd "%FRONTEND%"
    call npm install
    set "RC=%ERRORLEVEL%"
    popd
    if not "%RC%"=="0" ( echo [dev] ERROR: npm install failed. 1>&2 & exit /b 1 )
)

REM ---- start the Vite dev server (background, minimized console) ---------------
REM start /d sets the working directory (no cd/pushd inside the child command,
REM which would break on cmd's quote-stripping rules). The console window is
REM titled "HeliosView-Vite" so it can be found and killed with
REM taskkill /FI "WINDOWTITLE eq HeliosView-Vite*" when the app exits.
echo [dev] Vite dev server starting on port %PORT% (log: frontend\.vite.log)...
start "HeliosView-Vite" /min /d "%FRONTEND%" cmd /c ""npm run dev -- --port %PORT% --strictPort > "%LOGFILE%" 2>&1""

REM Wait until the server accepts connections (bounded; strictPort makes the URL stable)
for /l %%i in (1,1,120) do (
    curl -sf -o nul "%DEV_URL%" >nul 2>&1 && goto vite_ready
    >nul ping -n 2 127.0.0.1
)
echo [dev] ERROR: Dev server did not become ready on %DEV_URL% - see %LOGFILE% 1>&2
>nul taskkill /F /T /FI "WINDOWTITLE eq HeliosView-Vite*"
exit /b 1

:vite_ready
echo [dev] Dev server ready: %DEV_URL%

REM ---- configure + build the C++ app in dev mode --------------------------------
where ninja >nul 2>&1 && set "HAS_NINJA=1"
if defined HAS_NINJA (
    cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHELIOSVIEW_TEMPLATE_DEV=ON -DHELIOSVIEW_TEMPLATE_DEV_URL=%DEV_URL%
) else (
    cmake -S "%ROOT%" -B "%BUILD%" -A x64 -DHELIOSVIEW_TEMPLATE_DEV=ON -DHELIOSVIEW_TEMPLATE_DEV_URL=%DEV_URL%
)
if errorlevel 1 ( echo [dev] ERROR: CMake configure failed. 1>&2 & set "RC=1" & goto cleanup )
cmake --build "%BUILD%"
if errorlevel 1 ( echo [dev] ERROR: CMake build failed. 1>&2 & set "RC=1" & goto cleanup )

REM ---- run ------------------------------------------------------------------------
REM The executable name comes from app-config.cmake; find it in the build
REM output (bin\ holds exactly one .exe - the app).
for /f "delims=" %%E in ('dir /b "%BUILD%\bin\*.exe" 2^>nul') do set "APP_EXE=%%E"
if not defined APP_EXE (
    echo [dev] ERROR: no .exe found in %BUILD%\bin - did the build fail? 1>&2
    set "RC=1"
    goto cleanup
)
echo [dev] Running "%BUILD%\bin\%APP_EXE%" (close the window to stop)...
"%BUILD%\bin\%APP_EXE%"
set "RC=%ERRORLEVEL%"

:cleanup
>nul taskkill /F /T /FI "WINDOWTITLE eq HeliosView-Vite*" 2>&1
echo [dev] Dev server stopped.
exit /b %RC%
