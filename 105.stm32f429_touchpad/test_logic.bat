@echo off
rem ============================================================================
rem  105.stm32f429_touchpad - host-side logic tests (no board needed)
rem
rem  Compiles the pure-logic modules with the host gcc:
rem    app/gesture.c   + tools/test_gesture_mt.c    multi-touch state machine
rem    app/trackpad.c  + tools/test_trackpad_tap.c  tap-to-click decision
rem  FreeRTOS is stubbed by tools/hoststub/ and usb_mouse_*() by counters, so
rem  the firmware is not touched and this can run while the board is busy.
rem
rem    test_logic.bat
rem
rem  Exit codes: 0 all checks passed, 1 gcc missing, 2 build failed, 3 test failed
rem ============================================================================
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

where gcc >nul 2>nul
if errorlevel 1 (
  echo [FAIL] gcc not found in PATH
  exit /b 1
)

if not exist "build" mkdir "build"

echo [TEST] compiling tools/test_gesture_mt.c + app/gesture.c
gcc -std=c99 -Wall -Wextra -I app -o build\test_gesture_mt.exe tools\test_gesture_mt.c app\gesture.c
if errorlevel 1 (
  echo [FAIL] host build failed - gesture
  exit /b 2
)

echo [TEST] running gesture
build\test_gesture_mt.exe
if errorlevel 1 (
  echo [FAIL] gesture logic test failed
  exit /b 3
)

echo.
echo [TEST] compiling tools/test_trackpad_tap.c + app/trackpad.c
gcc -std=c99 -Wall -Wextra -I tools\hoststub -I app -o build\test_trackpad_tap.exe tools\test_trackpad_tap.c app\trackpad.c
if errorlevel 1 (
  echo [FAIL] host build failed - trackpad
  exit /b 2
)

echo [TEST] running trackpad tap-to-click
build\test_trackpad_tap.exe
if errorlevel 1 (
  echo [FAIL] trackpad logic test failed
  exit /b 3
)

echo.
echo [ OK ] logic tests passed
endlocal
