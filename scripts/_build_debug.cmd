@echo off
REM _build_debug.cmd - internal helper: Debug (Ninja) build of the app +
REM all plugins, the way CLion's MCP / scripts can invoke it without
REM nested-shell quoting. Reuses _toolchain.cmd for MSVC + cmake discovery.
call "%~dp0_toolchain.cmd"
if errorlevel 1 exit /b 1
"%CMAKE%" --build "%~dp0..\cmake-build-debug" --target GameTrigger -j 8
exit /b %errorlevel%