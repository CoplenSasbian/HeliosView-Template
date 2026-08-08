@echo off
setlocal
REM vite.cmd - run the Vite dev server for the frontend (dev mode, foreground;
REM Ctrl+C stops it). This is the script to use with CLion: dev mode is the
REM CMake default, so just run this script in a terminal, then build/run the
REM app from CLion - it loads http://localhost:5173 (HMR).
REM
REM   scripts\vite.cmd [-Port 5173]

set "PORT=5173"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-Port" (
    set "PORT=%~2"
    shift
    shift
    goto parse_args
)
echo [vite] ERROR: unknown option: %~1 1>&2
echo usage: vite.cmd [-Port 5173] 1>&2
exit /b 1
:args_done

REM Anchor to the script's own directory first: %~dp0 can be a relative path
REM depending on how the script is invoked, so nothing below may rely on the
REM caller's working directory.
cd /d "%~dp0" >nul 2>&1
pushd ".." >nul
set "ROOT=%CD%"
popd >nul
set "FRONTEND=%ROOT%\frontend"

if not exist "%FRONTEND%\package.json" (
    echo [vite] ERROR: frontend/ is not scaffolded yet. Run scripts\setup.cmd first. 1>&2
    exit /b 1
)
where node >nul 2>&1 || ( echo [vite] ERROR: Node.js is required ^(https://nodejs.org^). 1>&2 & exit /b 1 )

if not exist "%FRONTEND%\node_modules" (
    echo [vite] Installing frontend dependencies...
    pushd "%FRONTEND%"
    call npm install
    set "RC=%ERRORLEVEL%"
    popd
    if not "%RC%"=="0" ( echo [vite] ERROR: npm install failed. 1>&2 & exit /b 1 )
)

echo [vite] Vite dev server on http://localhost:%PORT% ^(HMR^) - Ctrl+C to stop.
pushd "%FRONTEND%"
call npm run dev -- --port %PORT% --strictPort
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
