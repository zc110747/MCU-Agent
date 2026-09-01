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

REM --- Step 1: required tool ---
set "TOOLMISS=0"
for %%T in (arduino-cli) do (
    where %%T >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Required tool not found: %%T
        set "TOOLMISS=1"
    )
)
if not "%TOOLMISS%"=="0" (
    echo         Install arduino-cli and add it to PATH: https://arduino.github.io/arduino-cli/
    set "ERR=1"
    goto END
)

REM --- Step 2: ensure required libraries (idempotent; offline-safe) ---
echo [STEP] Ensure libraries (ArduinoJson / PubSubClient / WebSockets)...
arduino-cli lib install ArduinoJson PubSubClient WebSockets >nul 2>&1 || echo [INFO] libraries already present (or offline - skipped)

REM --- Step 3: build ---
echo [STEP] arduino-cli compile...
set "FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600"
arduino-cli compile -j 8 -b "%FQBN%" --build-path ".build" "%~dp0."
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
