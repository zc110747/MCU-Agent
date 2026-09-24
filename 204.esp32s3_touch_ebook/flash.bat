@echo off
REM ============================================================
REM  One-click flash: build (incremental) then flash the
REM  firmware to the board over USB.  Mirrors the full idf.py
REM  output into build_log.txt and archives a timestamped copy
REM  into logs\.
REM
REM  Usage (double-click or from cmd):
REM      flash.bat              auto-detect the serial port
REM      flash.bat COM14        pin the serial port
REM
REM  Every exit path pauses so the window never closes on its
REM  own.  Automated runs may set BAT_NOPAUSE=1 in the
REM  environment to skip the pause.
REM ============================================================
setlocal EnableExtensions
cd /d "%~dp0"
if not exist logs mkdir logs

set "IDF_ARGS=flash"
if not "%~1"=="" set "IDF_ARGS=flash -p %~1"

echo.
echo === flash: idf.py %IDF_ARGS% ===
echo.

call tools\run_idf.bat %IDF_ARGS%
set "RC=%ERRORLEVEL%"

for /f "usebackq delims=" %%T in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"`) do set "TS=%%T"
if defined TS (
    copy /y build_log.txt "logs\flash_%TS%.log" >nul
) else (
    copy /y build_log.txt "logs\flash_last.log" >nul
)

if not "%RC%"=="0" goto fail

echo.
echo === flash: SUCCESS ===
echo Full log : build_log.txt  (copy archived in logs\)
echo.
if not defined BAT_NOPAUSE pause
endlocal & exit /b 0

:fail
echo.
echo === flash: FAILED (exit code %RC%) ===
echo Full log : build_log.txt  (open it for the error)
echo Hints   : check the USB cable, the port name, and that no
echo           other program holds the serial port open.
echo.
if not defined BAT_NOPAUSE pause
endlocal & exit /b %RC%
