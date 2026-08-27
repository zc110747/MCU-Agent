@echo off
REM ============================================================
REM  One-click build script
REM  Project : 009.stm32h743_zephyr
REM  Type    : STM32H7 Zephyr / west
REM  Flow    : clean -  configure+build west; configure integrated into build
REM  Notes   : English output only; every exit pauses.
REM ============================================================
setlocal
set "ERR=0"
cd /d "%~dp0"

echo ============================================================
echo [Build] 009.stm32h743_zephyr
echo ============================================================

REM --- Step 1: check required tools ---
set "TOOLMISS=0"
for %%T in (cmake ninja openocd arm-none-eabi-gcc python) do (
    where %%T >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Required tool not found: %%T
        set "TOOLMISS=1"
    )
)
if not "%TOOLMISS%"=="0" (
    echo         Please refer to document/support.md for installation instructions.
    set "ERR=1"
    goto END
)

REM --- Step 2: check required source directories ST only ---
REM no directory check for this project type

REM --- Step 3: build ---
echo [STEP] Clean west -t clean...
python -m west build -t clean -d build
if errorlevel 1 (
    echo [WARN] west clean failed or nothing to clean.
)
echo [STEP] Configure and Build west...
python -m west build -b nucleo_h743zi/stm32h743xx -d build -s .
if errorlevel 1 (
    echo [ERROR] Configure and Build west failed.
    set "ERR=1"
    goto END
)

echo.
if %ERR%==0 ( echo [DONE] Build succeeded. ) else ( echo [DONE] Build FAILED - see errors above. )
:END
pause
exit /b %ERR%
