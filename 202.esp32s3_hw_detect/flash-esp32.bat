@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM  flash-esp32.bat - flash + reset for ESP32-S3 (Arduino-CLI)
REM
REM  This script does NOT compile. Build the firmware first
REM  (build_oneclick.bat / arduino-cli compile) so that .build exists.
REM
REM  Usage (run from the project folder):
REM    flash-esp32.bat                flash, scan all COM ports and let you pick
REM    flash-esp32.bat COM7            flash on COM7 directly
REM    flash-esp32.bat COM7 monitor   flash on COM7 then open serial monitor
REM    flash-esp32.bat --no-pause ...  same, but never pause (for automated runs)
REM
REM  Notes:
REM    - arduino-cli lookup order: PATH -> local dir -> D:\data\agent-tools
REM    - board fixed to N16R8 (OPI PSRAM + 16MB Flash, 921600 upload)
REM    - esptool hard-resets after upload; an extra DTR/RTS pulse forces a
REM      clean power-on reset of the new firmware.
REM    - On ANY failure the script prints the real error and PAUSEs.
REM      Full output is saved to flash-esp32.log.
REM    - English output only.
REM ============================================================

set "FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600"
set "BAUD=115200"
set "SKETCH=."
set "BUILD=.build"
set "LOG=flash-esp32.log"
set "PORT="
set "TARGET=flash"

cd /d "%~dp0." || (
    echo [ERR] Cannot change to script directory: %~dp0.
    pause
    exit /b 1
)

REM --- parse args (order-independent) ---
set "NOPAUSE=0"
if /i "%~1"=="--no-pause" set "NOPAUSE=1"
if /i "%~2"=="--no-pause" set "NOPAUSE=1"
if /i "%~3"=="--no-pause" set "NOPAUSE=1"
if %NOPAUSE%==1 (set "PAUSECMD=echo [INFO] --no-pause: skipping pause") else (set "PAUSECMD=pause")

if /i "%~1"=="monitor"  set "TARGET=monitor"
if /i "%~2"=="monitor"  set "TARGET=monitor"
if /i "%~3"=="monitor"  set "TARGET=monitor"
if not "%~1"=="" if /i not "%~1"=="monitor" if /i not "%~1"=="--no-pause" set "PORT=%~1"
if not "%~2"=="" if /i not "%~2"=="monitor" if /i not "%~2"=="--no-pause" set "PORT=%~2"
if not "%~3"=="" if /i not "%~3"=="monitor" if /i not "%~3"=="--no-pause" set "PORT=%~3"

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

REM --- ensure firmware was built externally ---
if not exist "%BUILD%" (
    echo [ERR] Build directory "%BUILD%" not found.
    echo       Compile the firmware first (build_oneclick.bat), then re-run this script.
    %PAUSECMD%
    exit /b 1
)

REM --- resolve serial port: use arg directly, else scan & let user pick ---
if defined PORT (
    echo [INFO] Using supplied port: %PORT%
) else (
    call :scan_ports
)
if not defined PORT (
    %PAUSECMD%
    exit /b 1
)
echo [INFO] Port = %PORT%

REM --- flash (firmware already built externally) ---
echo [FLASH] Uploading to %PORT% @ 921600...
"%CLI%" upload -p %PORT% -b "%FQBN%" --build-path "%BUILD%" "%SKETCH%" > "%LOG%" 2>&1
if %errorlevel% neq 0 (
    echo.
    echo ============================================================
    echo [ERR] UPLOAD FAILED - last output from esptool:
    echo ============================================================
    type "%LOG%"
    echo.
    echo [ERR] Upload failed (exit %errorlevel%). Full log: %LOG%
    echo       Common causes: board not connected, wrong COM port, or port busy.
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

REM ============================================================
REM  :scan_ports - enumerate all serial ports, show a numbered
REM  list, and let the user pick one. Sets PORT on success.
REM ============================================================
:scan_ports
echo [INFO] No COM port supplied. Scanning all serial ports...
echo.
set "CNT=0"
set "PY="
where python >nul 2>&1
if %errorlevel%==0 set "PY=python"
if not defined PY where python3 >nul 2>&1
if %errorlevel%==0 set "PY=python3"
if not defined PY (
    echo [ERR] Python not found. Pass the COM port explicitly: flash-esp32.bat COMx
    goto :eof
)
"%PY%" -c "import serial.tools.list_ports as lp;[print(p.device+'|'+p.description) for p in lp.comports()]" > _ports.tmp 2>nul
if not exist _ports.tmp (
    echo [ERR] Serial port scan failed.
    goto :eof
)
for /f "tokens=1* delims=|" %%A in (_ports.tmp) do (
    set /a CNT+=1
    set "PORT_!CNT!=%%A"
    echo   [!CNT!] %%A  %%B
)
del /f _ports.tmp >nul 2>&1
if !CNT!==0 (
    echo [ERR] No serial port detected. Connect the board and retry.
    goto :eof
)
echo.
set /p "CHOICE=Select port number [1-!CNT!]: "
if not defined CHOICE (
    echo [ERR] No selection entered.
    goto :eof
)
set "PORT=!PORT_%CHOICE%!"
if not defined PORT (
    echo [ERR] Invalid selection: !CHOICE!
    goto :eof
)
goto :eof
