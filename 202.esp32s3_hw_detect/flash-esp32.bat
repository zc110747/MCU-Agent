@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM  flash-esp32.bat - one-click flash for ESP32-S3 (Arduino CLI)
REM  - auto-detects ESP32 port (USB VID 303A) when no COM given
REM  - or: flash-esp32.bat COM7          flash on COM7 directly
REM  - or: flash-esp32.bat COM7 monitor  flash then open monitor
REM  - or: flash-esp32.bat --no-pause     CI mode (no pause)
REM  Build the firmware first (build_oneclick.bat) so .build exists.
REM  English output only; every failure prints and PAUSEs.
REM ============================================================
set "FQBN=esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600"
set "BAUD=115200"
set "SKETCH=."
set "BUILD=.build"
set "LOG=flash-esp32.log"
set "PORT="
set "TARGET=flash"
set "NOPAUSE=0"

echo [flash-esp32] starting...
cd /d "%~dp0" || ( echo [ERR] cannot cd to %~dp0 & pause & exit /b 1 )

REM --- parse args (order-independent) ---
for %%A in (%*) do (
  if /i "%%A"=="--no-pause" set "NOPAUSE=1"
  if /i "%%A"=="monitor" set "TARGET=monitor"
  echo %%A | findstr /r /i "COM[0-9][0-9]*" >nul && set "PORT=%%A"
)
if %NOPAUSE%==1 (set "PAUSECMD=echo [INFO] --no-pause: skip pause") else (set "PAUSECMD=pause")

REM --- locate arduino-cli ---
set "CLI="
where arduino-cli >nul 2>&1 && set "CLI=arduino-cli"
if not defined CLI if exist "%~dp0arduino-cli.exe" set "CLI=%~dp0arduino-cli.exe"
if not defined CLI if exist "D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe" set "CLI=D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe"
if not defined CLI ( echo [ERR] arduino-cli not found ^(add to PATH or edit CLI block^) & %PAUSECMD% & exit /b 1 )
echo [INFO] CLI = %CLI%

REM --- ensure firmware was built externally ---
if not exist "%BUILD%" ( echo [ERR] .build missing - run build_oneclick.bat first & %PAUSECMD% & exit /b 1 )

REM --- resolve serial port ---
if defined PORT (
  echo [INFO] Using supplied port: %PORT%
) else (
  call :scan_ports
)
if not defined PORT ( %PAUSECMD% & exit /b 1 )
echo [INFO] Port = %PORT%

REM --- flash (firmware already built externally) ---
echo [FLASH] Uploading to %PORT% @ 921600 (may take 10-30s)...
echo [FLASH] full esptool output -> %LOG%
"%CLI%" upload -p %PORT% -b "%FQBN%" --build-path "%BUILD%" "%SKETCH%" > "%LOG%" 2>&1
set "RC=%errorlevel%"
type "%LOG%"
if %RC% neq 0 (
  echo.
  echo [ERR] UPLOAD FAILED (exit %RC%). Common: board not connected / wrong COM / port busy.
  %PAUSECMD%
  exit /b 1
)
echo [FLASH] PASS

REM --- reset pulse (harmless if not applicable) ---
echo [RESET] Reset pulse on %PORT%...
powershell -NoProfile -Command "$p=New-Object System.IO.Ports.SerialPort '%PORT%',115200; try { $p.Open(); $p.DtrEnable=$false; $p.RtsEnable=$false; Start-Sleep -Milliseconds 100; $p.RtsEnable=$true; Start-Sleep -Milliseconds 100; $p.DtrEnable=$true } catch {} finally { if($p.IsOpen){$p.Close()} }" >nul 2>&1
echo [RESET] done.

if /i "%TARGET%"=="monitor" (
  echo [MONITOR] %PORT% @ %BAUD% (Ctrl+C to quit)
  "%CLI%" monitor -p %PORT% -b %BAUD% --quiet
)
echo [DONE] Flashed on %PORT%.
%PAUSECMD%
exit /b 0

REM ============================================================
REM  :scan_ports - detect ESP32-S3 port without manual args.
REM  Primary: "arduino-cli board list" (no extra dependency).
REM  Fallback: python + pyserial (USB VID 303A match).
REM  NOTE: port lines are shown via delayed expansion OUTSIDE
REM  for-blocks, and row parsing goes through :scan_line/:py_line
REM  subroutines -- a ')' inside descriptions like "Serial Port
REM  (USB)" would otherwise close the for-block early.
REM  Sets PORT on success.
REM ============================================================
:scan_ports
set "CNT=0"
set "ESPCNT=0"
set "ESPPORT="
echo [INFO] Scanning serial ports...
"%CLI%" board list > _ports.tmp 2>nul
if exist _ports.tmp for /f "skip=1 tokens=1,*" %%P in (_ports.tmp) do call :scan_line "%%P" "%%Q"
if !CNT! gtr 0 goto scan_decide
echo [INFO] arduino-cli found no usable port, trying python/pyserial...
set "PY="
where python >nul 2>&1 && set "PY=python"
if not defined PY where python3 >nul 2>&1 && set "PY=python3"
set "PYOK=0"
if defined PY ( "%PY%" -c "import serial" >nul 2>&1 && set "PYOK=1" )
if not "%PYOK%"=="1" ( echo [ERR] no scan backend - pass COMx manually: flash-esp32.bat COMx & goto :eof )
"%PY%" -c "import serial.tools.list_ports as lp;[print('%s|%s|%s' % (p.device,p.description,p.hwid)) for p in lp.comports()]" > _ports.tmp 2>nul
if exist _ports.tmp for /f "usebackq tokens=1-3 delims=|" %%A in ("_ports.tmp") do call :py_line "%%A" "%%B" "%%C"

:scan_decide
if exist _ports.tmp del /f _ports.tmp >nul 2>&1
if !CNT!==0 ( echo [ERR] no serial port found - connect the board and retry & goto :eof )
if !ESPCNT!==1 (
  echo [AUTO] ESP32 detected at !ESPPORT! ^(USB VID 303A^)
  set "PORT=!ESPPORT!"
  goto :eof
)
if !ESPCNT! gtr 1 ( echo [INFO] multiple ESP32 ports found, pick one: ) else ( echo [WARN] no ESP32 auto-match, pick a port: )
for /l %%N in (1,1,!CNT!) do echo   [%%N] !PORT_%%N!  !REST_%%N!
set "CHOICE="
set /p "CHOICE=Select number [1-!CNT!]: "
if not defined CHOICE ( echo [ERR] no selection & goto :eof )
call set "PORT=%%PORT_!CHOICE!%%"
if not defined PORT ( echo [ERR] invalid selection: !CHOICE! & goto :eof )
goto :eof

REM --- row handler for "arduino-cli board list" ---
REM  %~1 = first token (must be COMx, skips indented multi-FQBN rows)
REM  %~2 = rest of row (contains Board Name / FQBN / Core)
:scan_line
echo %~1| findstr /r /i "^COM[0-9][0-9]*" >nul || goto :eof
set /a CNT+=1
set "PORT_%CNT%=%~1"
set "REST_%CNT%=%~2"
echo "%~2"| findstr /i "esp32:esp32" >nul && (
  set /a ESPCNT+=1
  set "ESPPORT=%~1"
)
goto :eof

REM --- row handler for pyserial scan ---
REM  %~1 = COMx, %~2 = description, %~3 = hwid (e.g. USB\VID_303A&PID_1001)
:py_line
set /a CNT+=1
set "PORT_%CNT%=%~1"
set "REST_%CNT%=%~2"
echo "%~3"| findstr /i "303A" >nul && (
  set /a ESPCNT+=1
  set "ESPPORT=%~1"
)
goto :eof
