@echo off
setlocal

REM ============================================================
REM  ESP32S3_FreeRTOS_Monitor - one-step build / flash / monitor
REM
REM  Usage - double-click or run in CMD inside the project folder
REM    build.bat                compile only, jobs=8 to limit heat
REM    build.bat flash          compile plus upload to default COM21
REM    build.bat flash COM21    compile plus upload to a given port
REM    build.bat upload COM21   same as flash
REM    build.bat monitor        open serial monitor only, 115200 baud
REM    build.bat all COM21      compile plus upload plus open monitor
REM
REM  Notes
REM    arduino-cli is auto-detected, PATH first then ..\.buildtools\bin
REM    if you use your own arduino-cli, add it to PATH and it is picked up
REM    board is fixed to N16R8, OPI PSRAM plus 16MB Flash
REM    a PAUSE is added at every exit so the window does not close at once
REM ============================================================

set "FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600"
set "PORT=COM21"
set "JOBS=8"
set "BAUD=115200"
set "SKETCH=%~dp0."
set "BUILD=%~dp0.build"

REM --- locate arduino-cli ---
set "CLI="
where arduino-cli >nul 2>&1
if %errorlevel%==0 set "CLI=arduino-cli"
if not defined CLI if exist "arduino-cli.exe" set "CLI=arduino-cli.exe"
if not defined CLI if exist "arduino-cli.exe" set "CLI=arduino-cli.exe"
if not defined CLI (
    echo [ERR] arduino-cli not found. Install it from https://arduino.github.io/arduino-cli/install/ or add it to PATH.
    pause
    exit /b 1
)
echo [INFO] CLI = %CLI%

REM --- parse arguments ---
set "TARGET=build"
if /i "%~1"=="flash"   set "TARGET=flash"
if /i "%~1"=="upload"  set "TARGET=flash"
if /i "%~1"=="monitor" set "TARGET=monitor"
if /i "%~1"=="all"     set "TARGET=all"
if not "%~2"=="" set "PORT=%~2"

REM --- monitor only ---
if /i "%TARGET%"=="monitor" goto :monitor

REM --- compile ---
echo [BUILD] Compiling, jobs=%JOBS%...
"%CLI%" compile -j %JOBS% -b "%FQBN%" --build-path "%BUILD%" "%SKETCH%"
if %errorlevel% neq 0 (
    echo [ERR] Build failed
    pause
    exit /b 1
)
echo [BUILD] PASS

if /i "%TARGET%"=="build" (
    echo [DONE] Output dir: %BUILD%
    pause
    exit /b 0
)

REM --- upload ---
echo [FLASH] Uploading to %PORT%...
"%CLI%" upload -p %PORT% -b "%FQBN%" --build-path "%BUILD%" "%SKETCH%"
if %errorlevel% neq 0 (
    echo [ERR] Upload failed. Port busy? Close serial monitor and retry.
    pause
    exit /b 1
)
echo [FLASH] PASS

if /i "%TARGET%"=="flash" (
    echo [DONE] Flashed to %PORT%
    pause
    exit /b 0
)

:monitor
echo [MONITOR] %PORT% @ %BAUD%, Ctrl+C to quit...
"%CLI%" monitor -p %PORT% -b %BAUD% --quiet
echo [MONITOR] Serial monitor closed.
pause
exit /b 0
