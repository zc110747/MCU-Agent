@echo off
REM ============================================================
REM  One-click build script
REM  Project : 102.stm32f429_tinyusb_ui
REM  Type    : STM32 plain CMake
REM  Flow    : configure -  clean -  build cmake -S . -B build; build
REM  Notes   : English output only; every exit pauses.
REM ============================================================
setlocal
set "ERR=0"
cd /d "%~dp0"

echo ============================================================
echo [Build] 102.stm32f429_tinyusb_ui
echo ============================================================

REM --- Step 1: check required tools ---
set "TOOLMISS=0"
for %%T in (cmake ninja openocd arm-none-eabi-gcc) do (
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
if not exist "Drivers" (
    echo [ERROR] Drivers directory not found in project root.
    echo         Extract Drivers from ..\support_tools\env_support_for_stm32f429.zip
    echo         Or copy ..\support_tools\env_support_for_stm32f429\Drivers into this folder, then re-run.
    set "ERR=1"
    goto END
)
if not exist "third_party" (
    echo [ERROR] third_party directory not found in project root.
    echo         Extract third_party from ..\support_tools\env_support_for_stm32f429.zip
    echo         Or copy ..\support_tools\env_support_for_stm32f429\third_party into this folder, then re-run.
    set "ERR=1"
    goto END
)

REM --- Step 3: build ---
echo [STEP] Configure...
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo [ERROR] Configure failed.
    set "ERR=1"
    goto END
)
echo [STEP] Clean...
cmake --build build --target clean
if errorlevel 1 (
    echo [ERROR] Clean failed.
    set "ERR=1"
    goto END
)
echo [STEP] Build...
cmake --build build
if errorlevel 1 (
    echo [ERROR] Build failed.
    set "ERR=1"
    goto END
)

echo.
if %ERR%==0 ( echo [DONE] Build succeeded. ) else ( echo [DONE] Build FAILED - see errors above. )
:END
pause
exit /b %ERR%
