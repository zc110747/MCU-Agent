@echo off
REM One-click flash for ESP32-S3 Debug Probe (native Windows cmd).
REM Usage:  flash.bat              (scan ports, default COM21 if present)
REM          flash.bat COM21       (explicit port)
REM          flash.bat COM21 921600 (port + baud)
REM Port list is printed to console; chosen port goes to idf.py flash.
setlocal
cd /d "%~dp0"
call "%~dp0env.bat"

set "ARG_PORT=%~1"
set "ARG_BAUD=%~2"
set "DEFAULT_PORT=COM21"
if "%ARG_BAUD%"=="" set "ARG_BAUD=460800"

set "CHOSEN="
for /f "usebackq delims=" %%i in (`"%PY%" "%~dp0tools\scan_port.py" "%ARG_PORT%" "%DEFAULT_PORT%"`) do set "CHOSEN=%%i"

if not defined CHOSEN (
    echo ERR: no port selected, abort
    exit /b 1
)

echo ==^> Flash port: %CHOSEN%   baud: %ARG_BAUD%
echo ==^> Target: esp32s3_debug_probe
"%PY%" "%~dp0idf_runner.py" flash -p "%CHOSEN%" -b %ARG_BAUD%
if errorlevel 1 (
    echo FLASH FAILED
    exit /b 1
)
echo ==^> Flash complete
endlocal
