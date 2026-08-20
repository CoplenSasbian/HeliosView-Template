@echo off
setlocal
REM build.cmd - release build for distribution (Windows only for now):
REM   1. builds the frontend (vite build)         -> frontend/dist
REM   2. builds the C++ app in prod mode           -> build\release\bin
REM
REM All runtime artifacts land in build\release\bin (see the top-level
REM CMakeLists.txt): the exe, HeliosView.dll, WebView2/OpenSSL dlls and
REM assets/. Copy the whole bin\ folder to distribute.
REM
REM   scripts\build.cmd
REM   Then run:  build\release\bin\HeliosViewApp.exe

REM Anchor to the script's own directory first.
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
echo The whole %BUILD%\bin\ folder is the distributable:
echo   exe + HeliosView.dll + WebView2/OpenSSL dlls + assets\
echo.
exit /b 0
