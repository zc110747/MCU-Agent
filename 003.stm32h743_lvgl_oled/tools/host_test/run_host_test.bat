@echo off
rem ===========================================================================
rem  Host-side acceptance test for the CTF + TTF font engine.
rem
rem  Compiles the REAL firmware sources (blkcache / ttf_reader / ctf_reader /
rem  stb_adapter) against a stdio shim that replaces FatFs, then runs them on
rem  real .ctf / .ttf files on the PC.  Catches logic bugs before flashing.
rem
rem  Usage:
rem    run_host_test.bat                 run every font under the SD image
rem    run_host_test.bat <dir-or-file>   run a single directory or .ttf
rem
rem  Requires: gcc (MSYS2 / MinGW) on PATH.
rem ===========================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
for %%I in ("%ROOT%\..\..") do set "PRJ=%%~fI"

set "SHIM=%ROOT%\host_shim"
set "STB=%PRJ%\third_party\lvgl\src\extra\libs\tiny_ttf"
set "OUT=%ROOT%\build"
set "EXE=%OUT%\ctf_host_test.exe"

if not exist "%OUT%" mkdir "%OUT%"

where gcc >nul 2>&1
if errorlevel 1 (
    echo [ERR ] gcc not found on PATH. Install MSYS2/MinGW and retry.
    exit /b 1
)

echo [1/2] compiling host test ...
gcc -std=c11 -O2 -Wall -Wextra ^
    -I "%PRJ%\Bsp\font" -I "%SHIM%" -I "%STB%" ^
    -o "%EXE%" ^
    "%ROOT%\ctf_host_test.c" ^
    "%PRJ%\Bsp\font\blkcache.c" ^
    "%PRJ%\Bsp\font\ttf_reader.c" ^
    "%PRJ%\Bsp\font\ctf_reader.c" ^
    "%PRJ%\Bsp\font\stb_adapter.c" ^
    -lm
if errorlevel 1 (
    echo [ERR ] compile failed.
    exit /b 1
)
echo [1/2] OK

echo.
echo [2/2] running ...
echo.

set "TARGET=%~1"
if "%TARGET%"=="" (
    set "TARGET=%PRJ%\..\support_tools\sd_card\SYSTEM"
)

if exist "%TARGET%\*.ttf" (
    set "FOUND=0"
    for %%F in ("%TARGET%\*.ttf") do (
        set "CTF=%%~dpnF.ctf"
        if exist "!CTF!" (
            set "FOUND=1"
            echo ---- %%~nF ----
            "%EXE%" "!CTF!" "%%~fF"
            if errorlevel 1 (
                echo [ERR ] %%~nF FAILED
                exit /b 1
            )
            echo.
        ) else (
            echo [SKIP] %%~nF : no matching .ctf - run tools/ttf2ctf/ttf2ctf.py first
        )
    )
    if "!FOUND!"=="0" (
        echo [ERR ] no .ctf + .ttf pair under "%TARGET%"
        exit /b 1
    )
) else (
    rem Single-file mode: pass the .ttf, its .ctf is derived by extension.
    "%EXE%" "%~dpn1.ctf" "%~f1"
    if errorlevel 1 exit /b 1
)

echo.
echo [DONE] all host tests passed.
exit /b 0
