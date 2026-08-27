@echo off
REM ============================================================
REM  One-click build script
REM  Project : 202.esp32s3_usb_wifi
REM  Type    : ESP32 ESP-IDF
REM  Flow    : set-target -  build idf.py
REM  Notes   : English output only; every exit pauses.
REM ============================================================
setlocal
set "ERR=0"
cd /d "%~dp0"

echo ============================================================
echo [Build] 202.esp32s3_usb_wifi
echo ============================================================

REM --- Step 1: check required tools ---
set "TOOLMISS=0"
for %%T in (idf.py) do (
    where %%T >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Required tool not found: %%T
        set "TOOLMISS=1"
    )
)
if not "%TOOLMISS%"=="0" (
    echo         ESP-IDF required: install ESP-IDF and ensure idf.py is in PATH.
    set "ERR=1"
    goto END
)

REM --- Step 2: check required source directories ST only ---
REM no directory check for this project type

REM --- Step 3: build ---
echo [STEP] Set target esp32s3...
idf.py set-target esp32s3
if errorlevel 1 (
    echo [WARN] set-target failed or already set.
)
echo [STEP] Build idf.py build...
idf.py build
if errorlevel 1 (
    echo [ERROR] Build idf.py build failed.
    set "ERR=1"
    goto END
)

echo.
if %ERR%==0 ( echo [DONE] Build succeeded. ) else ( echo [DONE] Build FAILED - see errors above. )
:END
pause
exit /b %ERR%
