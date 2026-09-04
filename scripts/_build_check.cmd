@echo off
set "CMAKE_COMMAND=E:\Microsoft Visual Studio\18 insider\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "CMAKE_MAKE_PROGRAM=E:\Microsoft Visual Studio\18 insider\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
call "%~dp0_toolchain.cmd"
if errorlevel 1 exit /b 1
"%CMAKE%" --build "%~dp0..\build\release" --config Release -j 8
if errorlevel 1 exit /b 1
exit /b 0