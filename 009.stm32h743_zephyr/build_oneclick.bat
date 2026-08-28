@echo off
setlocal
set "ERR=0"

REM Resolve project directory (strip trailing backslash to avoid \" quoting bug)
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
cd /d "%SCRIPT_DIR%"

echo ============================================================
echo [Build] 009.stm32h743_zephyr
echo ============================================================

REM --- Step 1: check required tools ---
set "TOOLMISS=0"
for %%T in (cmake ninja openocd arm-none-eabi-gcc python) do (
    where %%T > nul 2>nul
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

REM --- Step 2.5: Zephyr toolchain (gnuarmemb), derived from PATH (no hardcode) ---
set "ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb"
if not defined GNUARMEMB_TOOLCHAIN_PATH (
    for %%I in (arm-none-eabi-gcc) do set "ARM_GCC_PATH=%%~$PATH:I"
)
if defined ARM_GCC_PATH (
    for %%D in ("%ARM_GCC_PATH%\..") do set "GNUARMEMB_TOOLCHAIN_PATH=%%~fD"
)

REM --- Step 3: build ---
if exist build (
    echo [STEP] Clean west -t clean...
    python -m west build -t clean -d build
    if errorlevel 1 (
        echo [WARN] west clean failed or nothing to clean.
    )
) else (
    echo [STEP] Clean skipped: build dir not present - fresh build.
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
exit /b %ERR%
