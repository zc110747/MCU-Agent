@echo off
rem ============================================================================
rem  105.stm32f429_touchpad - single command-line entry point
rem
rem    build_oneclick.bat            Debug   -> build\          build, flash, run
rem    build_oneclick.bat release    Release -> build-release\  build, flash, run
rem    build_oneclick.bat all        both targets, flash the Debug image
rem    build_oneclick.bat build      build only, board untouched
rem    build_oneclick.bat flash      flash the existing ELF, no rebuild
rem    build_oneclick.bat test       host-side logic tests, no board needed
rem    build_oneclick.bat clean      wipe build\, build-release\, MDK-ARM\Objects\
rem    build_oneclick.bat help       this text
rem
rem  Switches, any position, combine freely with a mode:
rem    --no-flash     keep the board untouched
rem    --no-restart   flash, but do not restart the core
rem    --log          after the restart, capture the USART boot log
rem    --port COMx    serial port for --log (default COM3)
rem    --seconds N    capture window for --log (default 10)
rem
rem  Keil is not driven from here on purpose: open
rem  MDK-ARM\stm32f429_touchpad.uvprojx in uVision and build there (same layout
rem  as 101/102).  This script owns the CMake/Ninja route only.
rem
rem  Why the restart is hand-made: NRST is not wired on this board, so
rem  "reset run" does nothing, and a plain "reset halt" can leave the core in
rem  the ST system-memory bootloader.  The only reliable start is to write SP
rem  and PC from the ELF symbols and resume.
rem
rem  Exit codes: 0 ok, 1 missing tool or bad argument, 2 build failed
rem              (warnings included, zero-warning policy), 3 flash failed,
rem              4 restart failed.
rem ============================================================================
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

set "MODE="
set "NOFLASH="
set "NORESTART="
set "WANTLOG="
set "PORT=COM3"
set "SECS=10"

rem ------------------------------------------------------------- arguments ---
:args
if "%~1"=="" goto :args_done
set "A=%~1"
if /i "%A%"=="--no-flash"    goto :arg_noflash
if /i "%A%"=="--no-restart"  goto :arg_norestart
if /i "%A%"=="--log"         goto :arg_log
if /i "%A%"=="--port"        goto :arg_port
if /i "%A%"=="--seconds"     goto :arg_seconds
goto :arg_mode

:arg_noflash
set "NOFLASH=1"
shift
goto :args

:arg_norestart
set "NORESTART=1"
shift
goto :args

:arg_log
set "WANTLOG=1"
shift
goto :args

:arg_port
shift
set "PORT=%~1"
shift
goto :args

:arg_seconds
shift
set "SECS=%~1"
shift
goto :args

:arg_mode
if not defined MODE set "MODE=%A%"
shift
goto :args

:args_done
if not defined MODE set "MODE=debug"
if /i "%MODE%"=="help"  goto :usage
if /i "%MODE%"=="-h"    goto :usage
if /i "%MODE%"=="/?"    goto :usage
if /i "%MODE%"=="clean" goto :clean
if /i "%MODE%"=="test"  goto :logic_test
if /i "%MODE%"=="build" goto :build_only
if /i "%MODE%"=="flash" goto :flash_only
if /i "%MODE%"=="debug"   goto :go
if /i "%MODE%"=="release" goto :go
if /i "%MODE%"=="all"     goto :go

echo [FAIL] unknown mode "%MODE%"
goto :usage

rem ------------------------------------------------------------------ modes ---
:build_only
set "NOFLASH=1"
goto :go

:flash_only
set "MODE=skip"
goto :go

rem ------------------------------------------------------------------- main ---
:go
if not "%NOFLASH%"=="1" goto :need_flash_tools
goto :go_build

:need_flash_tools
where openocd >nul 2>nul
if errorlevel 1 (
  echo [FAIL] openocd not found in PATH
  exit /b 1
)
where arm-none-eabi-nm >nul 2>nul
if errorlevel 1 (
  echo [FAIL] arm-none-eabi-nm not found in PATH
  exit /b 1
)

:go_build
if /i "%MODE%"=="skip" goto :go_flash
set "BTV=debug"
if /i "%MODE%"=="release" set "BTV=release"
if /i "%MODE%"=="all"     set "BTV=all"
echo.
echo ===== build (%BTV%) =====
call :cmake_build %BTV%
if errorlevel 1 exit /b 2
if "%NOFLASH%"=="1" goto :done

:go_flash
set "ELF=build\stm32f429_touchpad.elf"
if not exist "%ELF%" (
  echo [FAIL] %ELF% is missing, build first
  exit /b 3
)

echo.
echo ===== flash =====
rem Forward slashes: TCL reads a backslash as an escape character.
openocd -f openocd.cfg -c "program build/stm32f429_touchpad.elf verify exit" > "build\flash.log" 2>&1
if errorlevel 1 (
  type "build\flash.log"
  echo [FAIL] openocd could not program the target
  echo        if the ST-Link is held open, run: taskkill /f /im openocd.exe
  exit /b 3
)
echo [ OK ] programmed and verified
if "%NORESTART%"=="1" goto :done

echo.
echo ===== restart at the flash vector =====
set "RESET="
set "STACK="
for /f "usebackq tokens=1,3" %%A in (`arm-none-eabi-nm "build\stm32f429_touchpad.elf"`) do (
  if "%%B"=="Reset_Handler" set "RESET=%%A"
  if "%%B"=="_estack" set "STACK=%%A"
)
if not defined RESET (
  echo [FAIL] Reset_Handler not found in the ELF
  exit /b 4
)
if not defined STACK (
  echo [FAIL] _estack not found in the ELF
  exit /b 4
)
echo [INFO] SP=0x%STACK% PC=0x%RESET%
openocd -f openocd.cfg -c "init; reset halt; reg sp 0x%STACK%; reg pc 0x%RESET%; resume; shutdown" > "build\restart.log" 2>&1
if errorlevel 1 (
  type "build\restart.log"
  echo [FAIL] could not restart the core
  exit /b 4
)
echo [ OK ] running

if not "%WANTLOG%"=="1" goto :done

echo.
echo ===== USART log on %PORT% for %SECS% s =====
where python >nul 2>nul
if errorlevel 1 (
  echo [WARN] python not found in PATH, skip the log
  goto :done
)
python tools\serial_capture.py --ports %PORT% --seconds %SECS%

:done
echo.
echo [ OK ] done
exit /b 0

rem ------------------------------------------------------------ cmake_build ---
rem %1 = debug | release | all.  Configures on first use, then builds and
rem enforces the zero-warning policy; prints the section sizes.
:cmake_build
set "BT=%~1"
where cmake >nul 2>nul
if errorlevel 1 (
  echo [FAIL] cmake not found in PATH
  exit /b 1
)
where ninja >nul 2>nul
if errorlevel 1 (
  echo [FAIL] ninja not found in PATH
  exit /b 1
)
where arm-none-eabi-gcc >nul 2>nul
if errorlevel 1 (
  echo [FAIL] arm-none-eabi-gcc not found in PATH
  exit /b 1
)

set "RC=0"
if /i "%BT%"=="debug" (
  call :build_one Debug build
  if errorlevel 1 set "RC=1"
)
if /i "%BT%"=="release" (
  call :build_one Release build-release
  if errorlevel 1 set "RC=1"
)
if /i "%BT%"=="all" (
  call :build_one Debug build
  if errorlevel 1 set "RC=1"
  call :build_one Release build-release
  if errorlevel 1 set "RC=1"
)
exit /b %RC%

rem %1 = Debug | Release   %2 = build directory
:build_one
set "CT=%~1"
set "BD=%~2"

if not exist "%BD%" mkdir "%BD%"

rem Release keeps PRINT_LOG compiled out: the format strings alone are worth
rem about 7 KB of FLASH.  Computed outside the if-block below, because %EXTRA%
rem would otherwise expand before it is assigned.
set "EXTRA="
if /i "%CT%"=="Release" set "EXTRA=-DENABLE_PRINT_LOG=OFF"

if not exist "%BD%\CMakeCache.txt" (
  echo [CONF ] %CT% -^> %BD%\
  cmake -S . -B "%BD%" -G Ninja -DCMAKE_BUILD_TYPE=%CT% %EXTRA% > "%BD%\configure.log" 2>&1
  if errorlevel 1 (
    type "%BD%\configure.log"
    echo [FAIL ] configure %CT%
    exit /b 2
  )
)

echo [BUILD] %CT%
cmake --build "%BD%" > "%BD%\build.log" 2>&1
if errorlevel 1 (
  type "%BD%\build.log"
  echo [FAIL ] build %CT%
  exit /b 3
)

rem set /a inside a for body is evaluated at run time, so no delayed expansion
rem is required and none is enabled.
set "WARN=0"
for /f "usebackq delims=" %%L in (`findstr /c:" warning:" "%BD%\build.log"`) do set /a WARN+=1

rem --print-memory-usage only prints when the linker actually runs, so keep the
rem last result and reuse it for incremental builds.
findstr /c:"FLASH:" "%BD%\build.log" >nul 2>nul
if not errorlevel 1 (
  findstr /c:"FLASH:" /c:"RAM:" /c:"CCMRAM:" /c:"SDRAM:" "%BD%\build.log" > "%BD%\size.txt"
)

echo -------- size (%CT%) --------
if exist "%BD%\size.txt" (
  type "%BD%\size.txt"
) else (
  echo   no link in this run
)
echo -----------------------------

if exist "%BD%\stm32f429_touchpad.elf" echo [ ELF ] %BD%\stm32f429_touchpad.elf

if not "%WARN%"=="0" (
  echo [FAIL ] %WARN% warning^(s^) - zero-warning policy^, see %BD%\build.log
  exit /b 5
)
echo [ OK  ] %CT%: 0 warnings
exit /b 0

rem ------------------------------------------------------------ logic_test ---
rem Same host builds the old test_logic.bat ran: the pure-logic modules are
rem compiled with the host gcc against tools/hoststub/, so no board is needed.
:logic_test
where gcc >nul 2>nul
if errorlevel 1 (
  echo [FAIL] gcc not found in PATH
  exit /b 1
)
if not exist "build" mkdir "build"

echo [TEST] gesture: tools\test_gesture_mt.c + app\gesture.c
gcc -std=c99 -Wall -Wextra -I app -o build\test_gesture_mt.exe tools\test_gesture_mt.c app\gesture.c
if errorlevel 1 (
  echo [FAIL] host build failed - gesture
  exit /b 2
)
build\test_gesture_mt.exe
if errorlevel 1 (
  echo [FAIL] gesture logic test failed
  exit /b 3
)

echo.
echo [TEST] trackpad: tools\test_trackpad_tap.c + app\trackpad.c
gcc -std=c99 -Wall -Wextra -I tools\hoststub -I app -o build\test_trackpad_tap.exe tools\test_trackpad_tap.c app\trackpad.c
if errorlevel 1 (
  echo [FAIL] host build failed - trackpad
  exit /b 2
)
build\test_trackpad_tap.exe
if errorlevel 1 (
  echo [FAIL] trackpad logic test failed
  exit /b 3
)

echo.
echo [ OK ] logic tests passed
exit /b 0

rem ---------------------------------------------------------------- clean ----
:clean
if exist build rmdir /s /q build
if exist build-release rmdir /s /q build-release
if exist MDK-ARM\Objects rmdir /s /q MDK-ARM\Objects
echo [ OK ] removed build\, build-release\, MDK-ARM\Objects\
exit /b 0

rem ---------------------------------------------------------------- usage ----
:usage
echo.
echo   build_oneclick.bat            Debug   - build, flash, run
echo   build_oneclick.bat release    Release - build, flash, run
echo   build_oneclick.bat all        both targets, flash the Debug image
echo   build_oneclick.bat build      build only, board untouched
echo   build_oneclick.bat flash      flash the existing ELF
echo   build_oneclick.bat test       host-side logic tests
echo   build_oneclick.bat clean      wipe all build directories
echo.
echo   Switches: --no-flash  --no-restart  --log  --port COMx  --seconds N
echo.
echo   Toolchain in PATH: cmake, ninja, arm-none-eabi-gcc.
echo   Flashing in PATH:  openocd, arm-none-eabi-nm.
echo   Keil: open MDK-ARM\stm32f429_touchpad.uvprojx in uVision.
echo.
exit /b 1
