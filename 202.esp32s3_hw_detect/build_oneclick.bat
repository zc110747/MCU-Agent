@echo off
REM ============================================================
REM  One-click build script
REM  Project : 202.esp32s3_hw_detect (ESP32-S3 Remote Hardware Debugger)
REM  Type    : ESP32 Arduino / arduino-cli
REM  Flow    : ensure libs -> arduino-cli compile
REM  Notes   : English output only; every failure pauses.
REM ============================================================
setlocal
set "ERR=0"
cd /d "%~dp0"

echo ============================================================
echo [Build] 202.esp32s3_hw_detect
echo ============================================================

REM --- Step 1: locate arduino-cli (PATH first, then known local install) ---
set "ARDUINO_CLI=arduino-cli"
where arduino-cli >nul 2>&1
if errorlevel 1 (
    if exist "D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe" (
        set "ARDUINO_CLI=D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe"
    ) else (
        echo [ERROR] arduino-cli not found in PATH or "D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit"
        echo         Install arduino-cli and add it to PATH: https://arduino.github.io/arduino-cli/
        set "ERR=1"
        goto END
    )
)
echo [INFO] Using arduino-cli: %ARDUINO_CLI%

REM --- Step 2: ensure required libraries (idempotent; offline-safe) ---
echo [STEP] Ensure libraries (ArduinoJson / PubSubClient / WebSockets / Adafruit NeoPixel)...
%ARDUINO_CLI% lib install ArduinoJson PubSubClient WebSockets "Adafruit NeoPixel" >nul 2>&1 || echo [INFO] libraries already present (or offline - skipped)

REM --- Step 3: build ---
echo [STEP] arduino-cli compile...
set "FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600"
REM arduino-cli caches compiled core/libraries automatically (in its data dir). Keeping
REM --build-path ".build" across runs makes incremental rebuilds much faster. Use
REM "rm -rf .build" (or "arduino-cli compile --clean") only for a forced full rebuild.
%ARDUINO_CLI% compile -j 8 -b "%FQBN%" --build-path ".build" "%~dp0."
if errorlevel 1 (
    echo [ERROR] Build failed.
    set "ERR=1"
    goto END
)

echo.
if %ERR%==0 ( echo [DONE] Build succeeded - firmware in .build ) else ( echo [DONE] Build FAILED - see errors above. )
:END
pause
exit /b %ERR%
