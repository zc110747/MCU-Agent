@echo off
REM ============================================================
REM  One-click build: compile the firmware with the ESP-IDF
REM  toolchain.  Mirrors the full idf.py output into
REM  build_log.txt and archives a timestamped copy into logs\.
REM
REM  Usage (double-click or from cmd):
REM      build_oneclick.bat
REM
REM  Every exit path pauses so the window never closes on its
REM  own.  Automated runs may set BAT_NOPAUSE=1 in the
REM  environment to skip the pause.
REM ============================================================
setlocal EnableExtensions
cd /d "%~dp0"
if not exist logs mkdir logs

echo.
echo === build_oneclick: idf.py build ===
echo.

call tools\run_idf.bat build
set "RC=%ERRORLEVEL%"

REM Archive the mirror log with a timestamp (success or failure).
for /f "usebackq delims=" %%T in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"`) do set "TS=%%T"
if defined TS (
    copy /y build_log.txt "logs\build_%TS%.log" >nul
) else (
    copy /y build_log.txt "logs\build_last.log" >nul
)

if not "%RC%"=="0" goto fail

echo.
echo === build_oneclick: SUCCESS ===
echo Full log : build_log.txt  (copy archived in logs\)
echo.
echo --- size / warnings from the log ---
findstr /I /C:"warning" /C:"binary size" /C:"Project build complete" build_log.txt
echo ------------------------------------
echo.
if not defined BAT_NOPAUSE pause
endlocal & exit /b 0

:fail
echo.
echo === build_oneclick: FAILED (exit code %RC%) ===
echo Full log : build_log.txt  (open it for the compiler error)
echo.
if not defined BAT_NOPAUSE pause
endlocal & exit /b %RC%
