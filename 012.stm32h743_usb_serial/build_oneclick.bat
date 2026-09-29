@echo off
setlocal EnableDelayedExpansion

REM ===========================================================================
REM  build_oneclick.bat - one-click build for 012.stm32h743_usb_serial
REM
REM  Project : STM32H743 USB CDC <-> UART4 bridge (no RTOS)
REM  Builds  : Debug and/or Release via CMake presets -> Ninja -> arm-none-eabi-gcc
REM
REM  Usage   : build_oneclick.bat                 (Debug + Release, then pause)
REM            build_oneclick.bat debug           (Debug only)
REM            build_oneclick.bat release         (Release only)
REM            build_oneclick.bat both            (same as no argument)
REM            build_oneclick.bat debug --no-pause (CI: no prompt)
REM
REM  Exit    : 0 = success, 1 = any failure
REM
REM  NOTE    : this file must stay pure ASCII and use CRLF line endings.
REM ===========================================================================

REM --- capture own directory BEFORE any shift (%~dp0 derives from %0) --------
set "BATDIR=%~dp0"
if "%BATDIR:~-1%"=="\" set "BATDIR=%BATDIR:~0,-1%"
set "PROJ=%BATDIR%"
pushd "%PROJ%" >nul 2>nul
set "PROJ=%CD%"
popd >nul 2>nul

set "MODE=both"
set "NOPAUSE="

REM ---------------------------------------------------------------------------
REM  Argument parsing
REM
REM  Two passes: first scan EVERY argument for --no-pause (so its position does
REM  not matter, e.g. "--help --no-pause" must still suppress the pause), then
REM  parse the single optional positional mode with a plain if/goto chain.
REM  Parenthesised blocks are avoided here on purpose: "shift" and "goto :label"
REM  behave unreliably inside them.
REM ---------------------------------------------------------------------------
for %%a in (%*) do if /i "%%~a"=="--no-pause" set "NOPAUSE=1"

:parse
if "%~1"=="" goto :parsed
if /i "%~1"=="--no-pause" shift & goto :parse
if /i "%~1"=="debug"   set "MODE=debug"   & shift & goto :parse
if /i "%~1"=="release" set "MODE=release" & shift & goto :parse
if /i "%~1"=="both"    set "MODE=both"    & shift & goto :parse
if /i "%~1"=="-h"      goto :usage
if /i "%~1"=="--help"  goto :usage
set "BADARG=%~1"
goto :usage
:parsed

if /i "%MODE%"=="debug"   goto :mode_ok
if /i "%MODE%"=="release" goto :mode_ok
if /i "%MODE%"=="both"    goto :mode_ok
set "BADARG=%MODE%"
goto :usage
:mode_ok

set "FAIL_STEP=0/5 setup"
set "FAIL_REASON="

echo ===========================================================================
echo  012.stm32h743_usb_serial - USB CDC ^<-^> UART4 bridge : one-click build
echo ===========================================================================
echo  Project    : %PROJ%
echo  Build mode : %MODE%
echo.

REM ===========================================================================
REM  STEP 1/5 - locate the toolchain
REM
REM  Resolution order: explicit override (env var or tools_env.bat) -> PATH.
REM  Everything else in this project (CMake presets, toolchain file) writes bare
REM  program names, so the toolchain must be reachable on PATH.
REM ===========================================================================
set "FAIL_STEP=1/5 toolchain"

if exist "%PROJ%\tools_env.bat" (
  echo [env] loading tools_env.bat
  call "%PROJ%\tools_env.bat"
)

REM arm-none-eabi-gcc / objcopy / size must come from the SAME bin directory.
set "GCC_FULL="
set "ARM_BIN="
for %%p in (arm-none-eabi-gcc.exe) do if not defined GCC_FULL set "GCC_FULL=%%~$PATH:p"
if defined GCC_FULL for %%d in ("%GCC_FULL%") do set "ARM_BIN=%%~dpd"
set "ARM_PREFIX="
if defined ARM_BIN set "ARM_PREFIX=%ARM_BIN%arm-none-eabi-"

REM Reject a same-named but foreign toolchain picked up from PATH.
if defined ARM_BIN echo %ARM_BIN% | findstr /i /c:"\Microchip\" /c:"\xc8" /c:"\xc16" /c:"\Keil" >nul
if not errorlevel 1 (
  set "FAIL_REASON=toolchain on PATH looks like a foreign vendor: %ARM_BIN%"
  set "FAIL_HINT1=Remove that directory from PATH, or set TOOLCHAIN_PREFIX yourself."
  goto :fail
)

if not defined GCC_FULL set "M1=  [REQUIRED] arm-none-eabi-gcc.exe - C compiler not found"
if defined M1 (set /a MISSING_N+=1 & set "MISSING_!MISSING_N!=!M1!")
if defined ARM_PREFIX if not exist "%ARM_PREFIX%objcopy.exe" set "M2=  [REQUIRED] arm-none-eabi-objcopy.exe - must sit next to arm-none-eabi-gcc.exe"
if defined M2 (set /a MISSING_N+=1 & set "MISSING_!MISSING_N!=!M2!")
if defined ARM_PREFIX if not exist "%ARM_PREFIX%size.exe" set "M3=  [REQUIRED] arm-none-eabi-size.exe - must sit next to arm-none-eabi-gcc.exe"
if defined M3 (set /a MISSING_N+=1 & set "MISSING_!MISSING_N!=!M3!")

where cmake >nul 2>nul || set "M4=  [REQUIRED] cmake.exe - CMake not found in PATH"
if defined M4 (set /a MISSING_N+=1 & set "MISSING_!MISSING_N!=!M4!")
where ninja >nul 2>nul || set "M5=  [REQUIRED] ninja.exe - Ninja generator not found in PATH"
if defined M5 (set /a MISSING_N+=1 & set "MISSING_!MISSING_N!=!M5!")

REM NOTE only: openocd is needed by the flash/erase/reset targets, not by a build.
where openocd >nul 2>nul || set "M6=  [NOTE] openocd.exe - not found, the flash/erase/reset targets will be disabled"
if defined M6 (set /a MISSING_N+=1 & set "MISSING_!MISSING_N!=!M6!")

if not defined MISSING_N goto :tools_ok
goto :missing
:tools_ok

echo [ok] arm-none-eabi-gcc : %GCC_FULL%
for /f "delims=" %%v in ('cmake --version 2^>nul') do if not defined CMAKE_V set "CMAKE_V=%%v"
echo [ok] %CMAKE_V%
for /f "delims=" %%v in ('ninja --version 2^>nul') do if not defined NINJA_V set "NINJA_V=%%v"
echo [ok] ninja            : %NINJA_V%
echo.

REM ===========================================================================
REM  STEP 2/5 - verify the project is complete
REM
REM  Drivers/ and third_party/ are excluded by the repository .gitignore, so a
REM  fresh checkout has them missing.  Check the exact directories CMakeLists.txt
REM  requires before touching cmake, otherwise the configure step fails with a
REM  wall of CMake output.
REM ===========================================================================
set "FAIL_STEP=2/5 project files"

set "DEP_N=0"
if not exist "%PROJ%\CMakeLists.txt"  set "D1=  [REQUIRED] CMakeLists.txt missing"
if defined D1 (set /a DEP_N+=1 & set "DEP_!DEP_N!=!D1!")
if not exist "%PROJ%\CMakePresets.json" set "D2=  [REQUIRED] CMakePresets.json missing"
if defined D2 (set /a DEP_N+=1 & set "DEP_!DEP_N!=!D2!")
if not exist "%PROJ%\cmake\arm-none-eabi.cmake" set "D3=  [REQUIRED] cmake\arm-none-eabi.cmake missing"
if defined D3 (set /a DEP_N+=1 & set "DEP_!DEP_N!=!D3!")
if not exist "%PROJ%\Drivers\STM32H7xx_HAL_Driver\Src" set "D4=  [REQUIRED] Drivers\STM32H7xx_HAL_Driver\Src missing - STM32H7 HAL sources"
if defined D4 (set /a DEP_N+=1 & set "DEP_!DEP_N!=!D4!")
if not exist "%PROJ%\Drivers\CMSIS\Core\Include\core_cm7.h" set "D5=  [REQUIRED] Drivers\CMSIS\Core\Include\core_cm7.h missing - CMSIS core headers"
if defined D5 (set /a DEP_N+=1 & set "DEP_!DEP_N!=!D5!")
if not exist "%PROJ%\third_party\tinyusb\src\tusb.c" set "D6=  [REQUIRED] third_party\tinyusb\src missing - TinyUSB sources"
if defined D6 (set /a DEP_N+=1 & set "DEP_!DEP_N!=!D6!")
if not exist "%PROJ%\sys_startup\stm32h743zi_flash.ld" set "D7=  [REQUIRED] sys_startup\stm32h743zi_flash.ld missing - linker script"
if defined D7 (set /a DEP_N+=1 & set "DEP_!DEP_N!=!D7!")
if not exist "%PROJ%\app\main.c" set "D8=  [REQUIRED] app\main.c missing"
if defined D8 (set /a DEP_N+=1 & set "DEP_!DEP_N!=!D8!")

REM Compare the counter, never "if defined DEP_N": a chained
REM "if defined X set /a ... & set ..." splits at parse time and the '&' is not
REM gated by the if, so DEP_N can end up defined-but-empty.
if !DEP_N! GTR 0 goto :deps_missing
echo [ok] project tree complete (app / bsp / Drivers / sys_startup / third_party)
echo.

REM ===========================================================================
REM  STEPS 3/5 .. 5/5 - configure, build and summarise each requested config
REM
REM  Done in a helper (:build_one) so each config runs at the TOP LEVEL of the
REM  script, not inside a deep parenthesised block.  Deep blocks in cmd are where
REM  delayed expansion, "shift", "goto :label" and "%VAR%" all start misbehaving.
REM  Configure defaults to debug; for MODE=release only release is built.
REM ===========================================================================
if /i "%MODE%"=="release" goto :build_release_only
call :build_one debug   "Debug (-Og -g3)"
if errorlevel 1 goto :fail
if /i not "%MODE%"=="both" goto :builds_done
call :build_one release "Release (-Os)"
if errorlevel 1 goto :fail
goto :builds_done

:build_release_only
call :build_one release "Release (-Os)"
if errorlevel 1 goto :fail

:builds_done

echo ===========================================================================
echo  BUILD OK
echo ===========================================================================
echo  Mode      : %MODE%
echo  Artifacts : build\debug\ and/or build\release\
echo              h743_usb_serial.elf / .hex / .bin
echo  Flash     : cmake --build --preset release --target flash   (needs openocd)
echo.
echo [OK] build_oneclick.bat completed successfully.
call :pause_exit
exit /b 0

REM ===========================================================================
REM  Helper sections (kept mid-file; reached with explicit call, never by fallthrough)
REM ===========================================================================
goto :eof

REM ---------------------------------------------------------------------------
REM  :build_one <preset> <display name>
REM  Configure + build one preset and print its size summary.
REM  Sets FAIL_STEP / FAIL_REASON / FAIL_HINTn and returns errorlevel 1 on
REM  failure; the caller performs the goto :fail (see skill pitfall: exit /b
REM  inside a CALL only ends the call).
REM  NOTE: "call :label" must not sit inside a parenthesised block, and every
REM  branch needs its own explicit exit /b - hence the goto-form below.
REM ---------------------------------------------------------------------------
:build_one
set "CFG_NAME=%~1"
set "CFG_DISPLAY=%~2"

set "FAIL_STEP=3/5 configure (%CFG_NAME%)"
echo [3/5] cmake --preset %CFG_NAME%
cmake --preset %CFG_NAME%
if errorlevel 1 goto :one_cfg_failed
echo [ok] configured: %CFG_DISPLAY%
echo.

set "FAIL_STEP=4/5 build (%CFG_NAME%)"
echo [4/5] cmake --build --preset %CFG_NAME%
cmake --build --preset %CFG_NAME%
if errorlevel 1 goto :one_build_failed
echo [ok] %CFG_NAME% build finished
echo.

set "FAIL_STEP=5/5 result (%CFG_NAME%)"
set "ELF=%PROJ%\build\%CFG_NAME%\h743_usb_serial.elf"
if not exist "%ELF%" goto :one_no_elf

set "ARM_SIZE=arm-none-eabi-size"
if defined ARM_PREFIX if exist "%ARM_PREFIX%size.exe" set "ARM_SIZE=%ARM_PREFIX%size.exe"
REM Read the "text data bss dec" row of arm-none-eabi-size.
REM NOTE: a QUOTED executable path does not work inside for /f backticks in this
REM environment (fails with "filename or directory syntax is incorrect"), so use
REM the bare name and rely on PATH.  The header row is dropped by comparing the
REM first token against "text".
set "SZ_TEXT="
set "SZ_DATA="
set "SZ_BSS="
set "SZ_DEC="
for /f "tokens=1-4" %%a in ('%ARM_SIZE% "%ELF%"') do if /i not "%%a"=="text" set "SZ_TEXT=%%a" & set "SZ_DATA=%%b" & set "SZ_BSS=%%c" & set "SZ_DEC=%%d"
echo --- size (%CFG_NAME%) : text=!SZ_TEXT!  data=!SZ_DATA!  bss=!SZ_BSS!  dec=!SZ_DEC! bytes
echo.
exit /b 0

:one_cfg_failed
set "FAIL_REASON=CMake configure failed for preset %CFG_NAME%"
set "FAIL_HINT1=Read the first CMake error above - it names the missing file or directory."
set "FAIL_HINT2=To rebuild from scratch: rmdir /s /q "%PROJ%\build\%CFG_NAME%""
exit /b 1

:one_build_failed
set "FAIL_REASON=compilation or link failed for preset %CFG_NAME%"
set "FAIL_HINT1=Scroll up to the first 'error:' line - later errors are usually fallout."
set "FAIL_HINT2=Full rebuild: rmdir /s /q "%PROJ%\build\%CFG_NAME%" then run this script again."
exit /b 1

:one_no_elf
set "FAIL_REASON=build reported success but %ELF% does not exist"
set "FAIL_HINT1=Check the CMake target name in CMakeLists.txt (expected h743_usb_serial)."
exit /b 1

REM ===========================================================================
REM  Failure + usage sections
REM ===========================================================================

:missing
echo [FAIL] prerequisite tools are missing:
echo.
for /l %%i in (1,1,%MISSING_N%) do echo !MISSING_%%i!
echo.
echo  Install the GNU Arm Embedded Toolchain and make sure its bin directory,
echo  cmake.exe and ninja.exe are all on PATH.
echo  Verify with:  arm-none-eabi-gcc --version
echo                cmake --version
echo                ninja --version
echo.
set "FAIL_REASON=missing prerequisite tool(s) - see the list above"
set "FAIL_HINT1=Add the toolchain bin directory to PATH, then open a new terminal."
set "FAIL_HINT2=CMake/Ninja come from MSYS2 mingw64 bin in this setup."
goto :fail

:deps_missing
echo [FAIL] project files are missing:
echo.
for /l %%i in (1,1,%DEP_N%) do echo !DEP_%%i!
echo.
echo  Drivers\ and third_party\ are excluded by the repository .gitignore, so a
echo  fresh checkout does not contain them.
echo  Copy them back from the shared environment package:
echo    support_tools\env_support_for_stm32h743\Drivers      to  %PROJ%\Drivers
echo    support_tools\env_support_for_stm32h743\third_party  to  %PROJ%\third_party
echo.
set "FAIL_REASON=project tree incomplete - see the list above"
set "FAIL_HINT1=Restore Drivers\ and third_party\ from support_tools\env_support_for_stm32h743."
goto :fail

:usage
if defined BADARG echo [FAIL] unknown argument: %BADARG%
if defined BADARG echo.
echo Usage: build_oneclick.bat [debug ^| release ^| both] [--no-pause]
echo.
echo   (no argument)  build Debug and Release   (default)
echo   debug          build Debug only
echo   release        build Release only
echo   --no-pause     do not wait for a key press (for CI)
echo.
if defined BADARG (
  set "FAIL_REASON=unknown command line argument: %BADARG%"
) else (
  set "FAIL_REASON=help requested"
)
set "FAIL_HINT1=Run the script with no argument to build Debug and Release."
call :pause_exit
if defined BADARG exit /b 1
exit /b 0

:fail
echo.
echo ===========================================================================
echo  BUILD FAILED
echo ===========================================================================
echo  Step   : %FAIL_STEP%
echo  Reason : %FAIL_REASON%
if defined FAIL_HINT1 echo  Hint 1 : %FAIL_HINT1%
if defined FAIL_HINT2 echo  Hint 2 : %FAIL_HINT2%
if defined FAIL_HINT3 echo  Hint 3 : %FAIL_HINT3%
echo.
call :pause_exit
exit /b 1

:pause_exit
if defined NOPAUSE exit /b 0
echo Press any key to close this window . . .
pause >nul
exit /b 0
