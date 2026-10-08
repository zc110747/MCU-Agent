@echo off
REM ---------------------------------------------------------------------------
REM One-click build for 013.stm32h743_virtual_uart (STM32H743 software UART)
REM
REM Usage:  build_oneclick.bat [debug^|release^|both]
REM Default: both
REM
REM Every exit path pauses, so the window stays open when launched by double
REM click. The pause and the exit /b are kept on separate lines: folding them
REM together lets the success path fall through into the failure label.
REM ---------------------------------------------------------------------------
setlocal

set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=both"

set "ROOT=%~dp0"
cd /d "%ROOT%"

if /i "%BUILD_TYPE%"=="debug" goto :debug
if /i "%BUILD_TYPE%"=="release" goto :release
if /i "%BUILD_TYPE%"=="both" goto :both
goto :usage

:debug
call :build debug
goto :done

:release
call :build release
goto :done

:both
call :build debug
if errorlevel 1 goto :fail
call :build release
if errorlevel 1 goto :fail
goto :done

:usage
echo Usage: %~nx0 [debug^|release^|both]
pause
exit /b 2

:build
echo.
echo ===========================================================
echo  Building %1
echo ===========================================================
cmake --preset %1
if errorlevel 1 exit /b 1
cmake --build --preset %1
if errorlevel 1 exit /b 1
echo.
echo [OK] %1 build finished.
exit /b 0

:fail
echo.
echo [FAIL] Build failed - see the log above.
pause
exit /b 1

:done
echo.
echo ===========================================================
echo  All requested builds finished.
echo  Flash with: cmake --build --preset debug --target flash
echo ===========================================================
pause
exit /b 0
