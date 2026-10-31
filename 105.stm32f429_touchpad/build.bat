@echo off
rem ============================================================================
rem  105.stm32f429_touchpad - one-key build (arm-none-eabi-gcc + CMake/Ninja)
rem
rem    build.bat              Debug   (default)
rem    build.bat release      Release
rem    build.bat all          Debug + Release
rem    build.bat clean        remove build/, build-release/, build_mdk/
rem    build.bat help         this text
rem
rem  Exit codes: 0 ok, 1 missing tool, 2 configure failed, 3 build failed,
rem              4 warnings present (zero-warning policy).
rem ============================================================================
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=debug"

if /i "%MODE%"=="help" goto :usage
if /i "%MODE%"=="-h"   goto :usage
if /i "%MODE%"=="/?"   goto :usage
if /i "%MODE%"=="clean" goto :clean
if /i "%MODE%"=="debug" goto :check_tools
if /i "%MODE%"=="release" goto :check_tools
if /i "%MODE%"=="all" goto :check_tools

echo [FAIL] unknown mode "%MODE%"
goto :usage

rem ---------------------------------------------------------------- tools ----
:check_tools
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
if /i "%MODE%"=="debug" (
  call :build_one Debug build
  if errorlevel 1 set "RC=1"
)
if /i "%MODE%"=="release" (
  call :build_one Release build-release
  if errorlevel 1 set "RC=1"
)
if /i "%MODE%"=="all" (
  call :build_one Debug build
  if errorlevel 1 set "RC=1"
  call :build_one Release build-release
  if errorlevel 1 set "RC=1"
)

echo.
if "%RC%"=="0" (
  echo [ OK ] done
) else (
  echo [FAIL] see the message above
)
exit /b %RC%

rem ------------------------------------------------------------ build_one ----
rem %1 = Debug | Release   %2 = build directory
:build_one
set "BT=%~1"
set "BD=%~2"

if not exist "%BD%" mkdir "%BD%"

rem Release keeps PRINT_LOG compiled out (README 4.x): the format strings
rem alone are worth ~7 KB of FLASH.  Computed outside the if-block below,
rem because %EXTRA% would otherwise expand before it is assigned.
set "EXTRA="
if /i "%BT%"=="Release" set "EXTRA=-DENABLE_PRINT_LOG=OFF"

if not exist "%BD%\CMakeCache.txt" (
  echo [CONF ] %BT% -^> %BD%\
  cmake -S . -B "%BD%" -G Ninja -DCMAKE_BUILD_TYPE=%BT% %EXTRA% > "%BD%\configure.log" 2>&1
  if errorlevel 1 (
    type "%BD%\configure.log"
    echo [FAIL ] configure %BT%
    exit /b 2
  )
)

echo [BUILD] %BT%
cmake --build "%BD%" > "%BD%\build.log" 2>&1
if errorlevel 1 (
  type "%BD%\build.log"
  echo [FAIL ] build %BT%
  exit /b 3
)

rem Warning count.  set /a inside a for body is evaluated at run time, so no
rem delayed expansion is required (and none is enabled).
set "WARN=0"
for /f "usebackq delims=" %%L in (`findstr /c:" warning:" "%BD%\build.log"`) do set /a WARN+=1

rem --print-memory-usage only prints when the linker actually runs, so keep the
rem last result and reuse it for incremental builds (sizes cannot change
rem without a relink).
findstr /c:"FLASH:" "%BD%\build.log" >nul 2>nul
if not errorlevel 1 (
  findstr /c:"FLASH:" /c:"RAM:" /c:"CCMRAM:" /c:"SDRAM:" "%BD%\build.log" > "%BD%\size.txt"
)

echo -------- size (%BT%) --------
if exist "%BD%\size.txt" (
  type "%BD%\size.txt"
) else (
  echo   no link in this run
)
echo -----------------------------

set "ELF=%BD%\stm32f429_touchpad.elf"
if exist "%ELF%" (
  echo [ ELF ] %ELF%
  echo [HEX/BIN] see %BD%\stm32f429_touchpad.hex and .bin
)

if not "%WARN%"=="0" (
  echo [FAIL ] %WARN% warning^(s^) - zero-warning policy^, see %BD%\build.log
  exit /b 4
)
echo [ OK  ] %BT%: 0 warnings
exit /b 0

rem ---------------------------------------------------------------- clean ----
:clean
if exist build rmdir /s /q build
if exist build-release rmdir /s /q build-release
if exist build_mdk rmdir /s /q build_mdk
echo [ OK  ] removed build\, build-release\, build_mdk\
exit /b 0

rem ---------------------------------------------------------------- usage ----
:usage
echo.
echo   build.bat            Debug build       (build\)
echo   build.bat release    Release build     (build-release\)
echo   build.bat all        both
echo   build.bat clean      wipe all build dirs
echo.
echo   Toolchain must be in PATH: cmake, ninja, arm-none-eabi-gcc.
echo.
exit /b 2
