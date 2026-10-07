@echo off
rem build.bat - configure and build firmware (Debug by default)
rem usage: build.bat [debug^|release^|clean]
setlocal
set "SCRIPT_DIR=%~dp0"
set "PROJ_ROOT=%SCRIPT_DIR%..\.."
if "%PROJ_ROOT:~-1%"=="\" set "PROJ_ROOT=%PROJ_ROOT:~0,-1%"
set "BUILD_DIR=%PROJ_ROOT%\build"

if /i "%1"=="clean" (
    echo Cleaning %BUILD_DIR%
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    echo Done.
    exit /b 0
)

set "BUILD_TYPE=Debug"
if /i "%1"=="release" set "BUILD_TYPE=Release"

cmake -G Ninja -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% || goto :err
cmake --build "%BUILD_DIR%" || goto :err
echo.
echo === %BUILD_TYPE% build OK ===
type "%BUILD_DIR%\size.txt"
exit /b 0

:err
echo.
echo === BUILD FAILED ===
exit /b 1
