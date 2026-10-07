@echo off
rem flash.bat - program firmware to RA8D1 Vision Board via pyOCD (ART-Link CMSIS-DAP)
rem usage: flash.bat [path\to\image.elf]  (default: build\firmware.elf)
setlocal
set "SCRIPT_DIR=%~dp0"
set "PROJ_ROOT=%SCRIPT_DIR%..\.."
if "%PROJ_ROOT:~-1%"=="\" set "PROJ_ROOT=%PROJ_ROOT:~0,-1%"
set "IMAGE=%~1"
if "%IMAGE%"=="" set "IMAGE=%PROJ_ROOT%\build\firmware.elf"
if not exist "%IMAGE%" (
    echo Image not found: %IMAGE%
    echo Build first: tools\build\build.bat
    exit /b 1
)
set "PYOCD=C:\Users\zc110\.workbuddy\binaries\python\envs\default\Scripts\python.exe"
if not exist "%PYOCD%" set "PYOCD=python"

"%PYOCD%" "%PROJ_ROOT%\tools\flash\flash.py" "%IMAGE%"
exit /b %ERRORLEVEL%
