@echo off
rem ============================================================================
rem  105.stm32f429_touchpad - host-side logic test (no board needed)
rem
rem  Compiles app/gesture.c with the host gcc and runs the multi-touch state
rem  machine checks in tools/test_gesture_mt.c.  The firmware itself is not
rem  touched, so this can run while the board is busy.
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
  echo [FAIL] host build failed
  exit /b 2
)

echo [TEST] running
build\test_gesture_mt.exe
if errorlevel 1 (
  echo [FAIL] logic test failed
  exit /b 3
)

echo [ OK ] logic test passed
endlocal
