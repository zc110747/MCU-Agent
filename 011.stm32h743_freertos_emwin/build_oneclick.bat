@echo off
REM ============================================================
REM  One-click build script
REM  Project : emwin_oled (STM32H743 + ST7789 + STemWin)
REM  Type    : STM32 plain CMake (emWin edition)
REM  Flow    : configure - clean - build (cmake -S . -B build)
REM  Notes   : English output only; every exit pauses.
REM ============================================================
setlocal
set "ERR=0"

REM Resolve script dir without trailing backslash (avoid \" escaped-quote parse error)
set "SD=%~dp0"
if "%SD:~-1%"=="\" set "SD=%SD:~0,-1%"
cd /d "%SD%"

echo ============================================================
echo [Build] emwin_oled - STM32H743 with ST7789 and STemWin
echo ============================================================

REM --- Step 1: check required tools ---
set "TOOLMISS=0"
for %%T in (cmake ninja openocd arm-none-eabi-gcc) do (
    where %%T > nul 2>nul
    if errorlevel 1 (
        echo [ERROR] Required tool not found: %%T
        set "TOOLMISS=1"
    )
)
if not "%TOOLMISS%"=="0" (
    echo         Please add cmake, ninja, openocd and arm-none-eabi-gcc to PATH, then re-run.
    set "ERR=1"
    goto END
)

REM --- Step 1b: prefer a binutils-lt-2.44 toolchain for STemWin compatibility ---
REM   The STemWin .a is armcc-built and only links with GNU ld below 2.44
REM   (GNU Arm Embedded 13.3.rel1 or 14.2.rel1). If such a toolchain is found
REM   via ARM_GNU_TOOLCHAIN, or at a well-known location, prepend its bin dir
REM   to PATH so cmake and flash use it. The CMake toolchain file performs the
REM   same auto-detection.
set "TC_BIN="
if defined ARM_GNU_TOOLCHAIN (
    if exist "%ARM_GNU_TOOLCHAIN%\bin\arm-none-eabi-gcc.exe" (
        set "TC_BIN=%ARM_GNU_TOOLCHAIN%\bin"
    )
)
if not defined TC_BIN (
    if exist "D:\Software\arm-gnu-toolchain-14.2.rel1\bin\arm-none-eabi-gcc.exe" (
        set "TC_BIN=D:\Software\arm-gnu-toolchain-14.2.rel1\bin"
    )
)
if not defined TC_BIN (
    if exist "C:\Software\arm-gnu-toolchain-14.2.rel1\bin\arm-none-eabi-gcc.exe" (
        set "TC_BIN=C:\Software\arm-gnu-toolchain-14.2.rel1\bin"
    )
)
REM NOTE: do NOT expand %PATH% inside a parenthesized block. The user PATH
REM contains ')' (e.g. "Program Files (x86)"), which cmd's block parser sees
REM BEFORE expansion and uses to prematurely close the block (parse error).
REM Keep the set outside any ( ) block so the quoted RHS is parsed literally.
if defined TC_BIN (
    echo [INFO] Prepending binutils-lt-2.44 toolchain to PATH: %TC_BIN%
)
if defined TC_BIN set "PATH=%TC_BIN%;%PATH%"
if not defined TC_BIN (
    echo [WARN] No binutils-lt-2.44 toolchain found; STemWin link may fail on ld 2.44 plus.
)

REM --- Step 1c: wipe stale build dir if it was configured with a different compiler ---
REM   Avoids a cached 15.3.1 toolchain (ld 2.44 plus) silently failing the link.
if defined TC_BIN (
    if exist "build\CMakeCache.txt" (
        findstr /C:"arm-gnu-toolchain-14.2" build\CMakeCache.txt >nul 2>&1
        if errorlevel 1 (
            echo [INFO] Stale build dir not using the compatible toolchain; removing build to reconfigure.
            rd /s /q build
        )
    )
)

REM --- Step 2: check required source directories ---
if not exist "Drivers" (
    echo [ERROR] Drivers directory not found in project root.
    echo         Copy the STM32H7 HAL and CMSIS Drivers into this folder, then re-run.
    set "ERR=1"
    goto END
)
if not exist "third_party" (
    echo [ERROR] third_party directory not found in project root.
    echo         Copy third_party - FatFs and STemWin - into this folder, then re-run.
    set "ERR=1"
    goto END
)
if not exist "third_party\STemWin\Lib\STemWin_CM7_wc16.a" (
    echo [ERROR] STemWin library not found: third_party\STemWin\Lib\STemWin_CM7_wc16.a
    echo         Copy the official STemWin distribution from STM32CubeMX into third_party\STemWin, then re-run.
    set "ERR=1"
    goto END
)

REM --- Step 3: build both Debug and Release (configure - clean - build) ---
set "TC_FILE=%~dp0cmake\gcc-arm-none-eabi.cmake"

REM --- Debug ---
echo [STEP] Configure Debug...
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=%TC_FILE%
if errorlevel 1 (
    echo [ERROR] Configure Debug failed.
    set "ERR=1"
    goto END
)
echo [STEP] Clean Debug...
cmake --build build --target clean
if errorlevel 1 (
    echo [ERROR] Clean Debug failed.
    set "ERR=1"
    goto END
)
echo [STEP] Build Debug...
cmake --build build
if errorlevel 1 (
    echo [ERROR] Build Debug failed.
    set "ERR=1"
    goto END
)

REM --- Release ---
echo [STEP] Configure Release...
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=%TC_FILE%
if errorlevel 1 (
    echo [ERROR] Configure Release failed.
    set "ERR=1"
    goto END
)
echo [STEP] Clean Release...
cmake --build build-rel --target clean
if errorlevel 1 (
    echo [ERROR] Clean Release failed.
    set "ERR=1"
    goto END
)
echo [STEP] Build Release...
cmake --build build-rel
if errorlevel 1 (
    echo [ERROR] Build Release failed.
    set "ERR=1"
    goto END
)

echo.
if %ERR%==0 ( echo [DONE] Build succeeded. ) else ( echo [DONE] Build FAILED - see errors above. )
:END
pause
exit /b %ERR%
