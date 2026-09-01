@echo off
setlocal
REM ============================================================
REM  flash-esp32.bat - one-click build + flash + reset for ESP32-S3
REM
REM  Usage (run from the project folder):
REM    flash-esp32.bat                      build + flash, auto-detect COM port
REM    flash-esp32.bat COM7                 build + flash on COM7
REM    flash-esp32.bat COM7 monitor         build + flash + open serial monitor
REM    flash-esp32.bat build                compile only (no flash)
REM    flash-esp32.bat --no-pause ...       same, but never pause (for automated runs)
REM
REM  Notes:
REM    - arduino-cli lookup order: PATH -> local dir -> D:\data\agent-tools
REM    - board fixed to N16R8 (OPI PSRAM + 16MB Flash, 921600 upload)
REM    - esptool hard-resets the chip after upload; an extra DTR/RTS
REM      pulse then forces a clean power-on reset of the new firmware.
REM    - On ANY failure the script prints the real error and PAUSEs so
REM      the cause is visible. Full output is saved to flash-esp32.log.
REM    - English output only.
REM ============================================================

set "FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600"
set "JOBS=8"
set "BAUD=115200"
set "SKETCH=."
set "BUILD=.build"
set "LOG=flash-esp32.log"
set "PORT="
set "TARGET=flash"

REM --- cd to the script directory so all paths are relative ---
REM NOTE: use "%~dp0." (append a dot) so the trailing backslash of %~dp0
REM does NOT escape the closing quote in cmd.
cd /d "%~dp0." || (
    echo [ERR] Cannot change to script directory: %~dp0.
    %PAUSECMD%
    exit /b 1
)

REM --- parse args (order-independent) ---
set "NOPAUSE=0"
if /i "%~1"=="--no-pause" set "NOPAUSE=1"
if /i "%~2"=="--no-pause" set "NOPAUSE=1"
if /i "%~3"=="--no-pause" set "NOPAUSE=1"
if %NOPAUSE%==1 (set "PAUSECMD=echo [INFO] --no-pause: skipping pause") else (set "PAUSECMD=pause")

if /i "%~1"=="build"    set "TARGET=build"
if /i "%~2"=="build"    set "TARGET=build"
if /i "%~3"=="build"    set "TARGET=build"
if /i "%~1"=="monitor"  set "TARGET=monitor"
if /i "%~2"=="monitor"  set "TARGET=monitor"
if /i "%~3"=="monitor"  set "TARGET=monitor"
if not "%~1"=="" if /i not "%~1"=="build" if /i not "%~1"=="monitor" if /i not "%~1"=="--no-pause" set "PORT=%~1"
if not "%~2"=="" if /i not "%~2"=="build" if /i not "%~2"=="monitor" if /i not "%~2"=="--no-pause" set "PORT=%~2"
if not "%~3"=="" if /i not "%~3"=="build" if /i not "%~3"=="monitor" if /i not "%~3"=="--no-pause" set "PORT=%~3"

REM --- locate arduino-cli ---
set "CLI="
where arduino-cli >nul 2>&1
if %errorlevel%==0 set "CLI=arduino-cli"
if not defined CLI if exist "%~dp0arduino-cli.exe" set "CLI=%~dp0arduino-cli.exe"
if not defined CLI if exist "D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe" set "CLI=D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe"
if not defined CLI (
    echo [ERR] arduino-cli not found. Add it to PATH or edit the CLI lookup block.
    %PAUSECMD%
    exit /b 1
)
echo [INFO] CLI = %CLI%

REM --- compile ---
echo [BUILD] Compiling, jobs=%JOBS%...
"%CLI%" compile -j %JOBS% -b "%FQBN%" --build-path "%BUILD%" "%SKETCH%" > "%LOG%" 2>&1
if %errorlevel% neq 0 (
    echo.
    echo ============================================================
    echo [ERR] BUILD FAILED - last output from arduino-cli:
    echo ============================================================
    type "%LOG%"
    echo.
    echo [ERR] Build failed (exit %errorlevel%^). Full log: %LOG%
    echo       Common causes: missing bsp/ module, wrong FQBN, or a code error above.
    %PAUSECMD%
    exit /b 1
)
call :showlog
echo [BUILD] PASS

if /i "%TARGET%"=="build" (
    echo [DONE] Build only. Firmware at %BUILD%.
    %PAUSECMD%
    exit /b 0
)

REM --- auto-detect serial port when not given (flash / monitor only) ---
if not defined PORT (
    for /f "delims=" %%P in ('powershell -NoProfile -Command "Get-WmiObject Win32_SerialPort | Where-Object {$_.PNPDeviceID -match 'CH343|1A86'} | Select-Object -ExpandProperty DeviceID"') do if not defined PORT set "PORT=%%P"
    if defined PORT (
        echo [INFO] Detected CH343 port: %PORT%
    ) else (
        echo [INFO] No CH343 found, using the first serial port...
        for /f "delims=" %%P in ('powershell -NoProfile -Command "Get-WmiObject Win32_SerialPort | Select-Object -ExpandProperty DeviceID"') do if not defined PORT set "PORT=%%P"
    )
)
if not defined PORT (
    echo [ERR] No serial port detected. Pass one explicitly: flash-esp32.bat COMx
    %PAUSECMD%
    exit /b 1
)
echo [INFO] Port = %PORT%

REM --- flash ---
echo [FLASH] Uploading to %PORT% @ 921600...
"%CLI%" upload -p %PORT% -b "%FQBN%" --build-path "%BUILD%" "%SKETCH%" > "%LOG%" 2>&1
if %errorlevel% neq 0 (
    echo.
    echo ============================================================
    echo [ERR] UPLOAD FAILED - last output from esptool:
    echo ============================================================
    type "%LOG%"
    echo.
    echo [ERR] Upload failed (exit %errorlevel%^). Full log: %LOG%
    echo       Common causes: board not connected, wrong COM port, or port busy by another tool.
    %PAUSECMD%
    exit /b 1
)
echo [FLASH] PASS

REM --- reset: extra DTR/RTS pulse for a clean power-on reset ---
echo [RESET] Resetting %PORT%...
powershell -NoProfile -Command "$p=New-Object System.IO.Ports.SerialPort '%PORT%',115200; $p.Open(); $p.DtrEnable=$false; $p.RtsEnable=$false; Start-Sleep -Milliseconds 120; $p.RtsEnable=$true; Start-Sleep -Milliseconds 120; $p.DtrEnable=$true; $p.Close()" >nul 2>&1
if %errorlevel% neq 0 (
    echo [WARN] Manual reset pulse failed; the chip was already reset by esptool.
) else (
    echo [RESET] PASS - device restarted.
)

if /i "%TARGET%"=="monitor" (
    echo [MONITOR] Opening %PORT% @ %BAUD%, Ctrl+C to quit...
    "%CLI%" monitor -p %PORT% -b %BAUD% --quiet
    echo [MONITOR] Serial monitor closed.
)

echo [DONE] Flashed and restarted on %PORT%.
%PAUSECMD%
exit /b 0

REM --- helper: print the last 40 lines of the log on success ---
:showlog
powershell -NoProfile -Command "Get-Content '%LOG%' | Select-Object -Last 40"
exit /b
