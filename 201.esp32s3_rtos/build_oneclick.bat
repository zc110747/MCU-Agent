@echo off
REM ============================================================
REM  One-click build script
REM  Project : 201.esp32s3_rtos
REM  Type    : ESP32 Arduino / arduino-cli
REM  Flow    : build arduino-cli compile
REM  Notes   : English output only; every exit pauses.
REM ============================================================
setlocal
set "ERR=0"
cd /d "%~dp0"

echo ============================================================
echo [Build] 201.esp32s3_rtos
echo ============================================================

REM --- Step 1: check required tools ---
set "TOOLMISS=0"
for %%T in (arduino-cli) do (
    where %%T >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Required tool not found: %%T
        set "TOOLMISS=1"
    )
)
if not "%TOOLMISS%"=="0" (
    echo         Install arduino-cli and add it to PATH https://arduino.github.io/arduino-cli/.
    set "ERR=1"
    goto END
)

REM --- Step 2: check required source directories ST only ---
REM no directory check for this project type

REM --- Step 3: build ---
echo [STEP] Build arduino-cli compile...
set "FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600"
arduino-cli compile -j 8 -b "%FQBN%" --build-path ".build" "%~dp0."
if errorlevel 1 (
    echo [ERROR] Build arduino-cli compile failed.
    set "ERR=1"
    goto END
)

echo.
if %ERR%==0 ( echo [DONE] Build succeeded. ) else ( echo [DONE] Build FAILED - see errors above. )
:END
pause
exit /b %ERR%
