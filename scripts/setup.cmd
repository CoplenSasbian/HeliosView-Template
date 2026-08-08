@echo off
setlocal
REM setup.cmd - scaffold the frontend with your framework of choice.
REM (Windows, pure cmd - no PowerShell involved; macOS/Linux: setup.sh)
REM
REM The repo ships with a Vue frontend in frontend/ (works out of the box); run
REM this script to switch frameworks. Uses the official Vite templates (react /
REM vue / svelte / solid / preact / lit / vanilla, each in JS or TS), so every
REM scaffold stays up to date.
REM
REM   scripts\setup.cmd                            interactive menu
REM   scripts\setup.cmd -Template vue-ts           non-interactive
REM   scripts\setup.cmd -Template react-ts -Force  replace the existing frontend

set "TEMPLATE="
set "FORCE="

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="-Template" (
    set "TEMPLATE=%~2"
    shift
    shift
    goto parse_args
)
if /i "%~1"=="-Force" (
    set "FORCE=1"
    shift
    goto parse_args
)
echo [setup] ERROR: unknown option: %~1 1>&2
echo usage: setup.cmd [-Template name] [-Force] 1>&2
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

where node >nul 2>&1 || ( echo [setup] ERROR: Node.js is required ^(https://nodejs.org^). 1>&2 & exit /b 1 )
where npm  >nul 2>&1 || ( echo [setup] ERROR: npm not found ^(it ships with Node.js^). 1>&2 & exit /b 1 )

if exist "%FRONTEND%\package.json" (
    if not defined FORCE (
        echo [setup] ERROR: frontend/ already exists ^(ships with a Vue app^). 1>&2
        echo           Re-scaffold it with -Force ^(this deletes frontend/^). 1>&2
        exit /b 1
    )
    echo [setup] -Force: removing existing frontend/...
    rmdir /s /q "%FRONTEND%"
)

REM ---- template selection ----------------------------------------------------
if not "%TEMPLATE%"=="" goto have_template
echo.
echo Pick a frontend framework (Vite template):
echo   [ 0] react-ts      React + TypeScript
echo   [ 1] react         React ^(JavaScript^)
echo   [ 2] vue-ts        Vue + TypeScript
echo   [ 3] vue           Vue ^(JavaScript^)
echo   [ 4] svelte-ts     Svelte + TypeScript
echo   [ 5] svelte        Svelte ^(JavaScript^)
echo   [ 6] solid-ts      Solid + TypeScript
echo   [ 7] solid         Solid ^(JavaScript^)
echo   [ 8] preact-ts     Preact + TypeScript
echo   [ 9] preact        Preact ^(JavaScript^)
echo   [10] lit-ts        Lit + TypeScript
echo   [11] lit           Lit ^(JavaScript^)
echo   [12] vanilla-ts    Vanilla TS ^(no framework^)
echo   [13] vanilla       Vanilla JS ^(no framework^)
set /p "CHOICE=Enter a number (default 0: react-ts): "
if "%CHOICE%"=="" set "CHOICE=0"
if "%CHOICE%"=="0"  set "TEMPLATE=react-ts"
if "%CHOICE%"=="1"  set "TEMPLATE=react"
if "%CHOICE%"=="2"  set "TEMPLATE=vue-ts"
if "%CHOICE%"=="3"  set "TEMPLATE=vue"
if "%CHOICE%"=="4"  set "TEMPLATE=svelte-ts"
if "%CHOICE%"=="5"  set "TEMPLATE=svelte"
if "%CHOICE%"=="6"  set "TEMPLATE=solid-ts"
if "%CHOICE%"=="7"  set "TEMPLATE=solid"
if "%CHOICE%"=="8"  set "TEMPLATE=preact-ts"
if "%CHOICE%"=="9"  set "TEMPLATE=preact"
if "%CHOICE%"=="10" set "TEMPLATE=lit-ts"
if "%CHOICE%"=="11" set "TEMPLATE=lit"
if "%CHOICE%"=="12" set "TEMPLATE=vanilla-ts"
if "%CHOICE%"=="13" set "TEMPLATE=vanilla"
if "%TEMPLATE%"=="" ( echo [setup] ERROR: Invalid choice: %CHOICE% 1>&2 & exit /b 1 )
:have_template

REM ---- scaffold ----------------------------------------------------------------
echo [setup] Scaffolding frontend with template '%TEMPLATE%'...
pushd "%ROOT%"
call npm create --yes vite@latest frontend -- --template %TEMPLATE%
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" ( echo [setup] ERROR: npm create vite failed. 1>&2 & exit /b 1 )

echo [setup] Installing dependencies...
pushd "%FRONTEND%"
call npm install
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" ( echo [setup] ERROR: npm install failed. 1>&2 & exit /b 1 )

REM ---- bridge typings (TypeScript templates only) -----------------------------
set "SUFFIX=%TEMPLATE:~-3%"
if /i "%SUFFIX%"=="-ts" (
    copy /y "%~dp0helios.d.ts" "%FRONTEND%\src\helios.d.ts" >nul
    echo [setup] Wrote frontend/src/helios.d.ts ^(bridge typings^).
)

echo.
echo [setup] Done. Next steps:
echo   1. Dev loop :  scripts\dev.cmd     ^(C++ app + Vite dev server, HMR^)
echo   2. Release  :  scripts\build.cmd   ^(C++ app + built frontend^)
echo.
echo   Frontend structure:  frontend/  ^(Vite project, output: frontend/dist^)
exit /b 0
