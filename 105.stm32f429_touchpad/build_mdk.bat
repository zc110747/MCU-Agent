@echo off
rem ============================================================================
rem  105.stm32f429_touchpad - one-key build with MDK-ARM (uVision / armclang)
rem
rem    build_mdk.bat              Debug   (build_mdk\Debug\)
rem    build_mdk.bat release      Release (build_mdk\Release\)
rem    build_mdk.bat all          both
rem    build_mdk.bat gen          regenerate the .uvprojx file list only
rem    build_mdk.bat clean        remove build_mdk\
rem    build_mdk.bat help         this text
rem
rem  Add "gen" to any build form to regenerate the project first, e.g.
rem    build_mdk.bat all gen
rem
rem  Needs UV4.exe in PATH (Keil uVision).  UV4 exit codes:
rem    0 ok, 1 warnings, 2 errors, 3 fatal, 11 cannot open project,
rem    12 device not found, 13 cannot write project, 15 bad XML.
rem ============================================================================
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

set "MODE=%~1"
set "REGEN=%~2"
if "%MODE%"=="" set "MODE=debug"

if /i "%MODE%"=="help" goto :usage
if /i "%MODE%"=="-h"   goto :usage
if /i "%MODE%"=="/?"   goto :usage
if /i "%MODE%"=="clean" goto :clean
if /i "%MODE%"=="gen" goto :regen_only
if /i "%MODE%"=="debug" goto :check_tools
if /i "%MODE%"=="release" goto :check_tools
if /i "%MODE%"=="all" goto :check_tools

echo [FAIL] unknown mode "%MODE%"
goto :usage

rem ---------------------------------------------------------------- tools ----
:check_tools
where UV4.exe >nul 2>nul
if errorlevel 1 (
  echo [FAIL] UV4.exe not found in PATH ^(add the Keil uVision folder^)
  exit /b 1
)
if not exist "stm32f429_touchpad.sct" (
  echo [FAIL] stm32f429_touchpad.sct is missing
  exit /b 1
)

if /i "%REGEN%"=="gen" call :regen
if not exist "stm32f429_touchpad.uvprojx" (
  echo [INFO] project file missing, generating it
  call :regen
)
if not exist "stm32f429_touchpad.uvprojx" (
  echo [FAIL] could not generate stm32f429_touchpad.uvprojx
  exit /b 1
)

set "RC=0"
if /i "%MODE%"=="debug" (
  call :build_one Debug
  if errorlevel 1 set "RC=1"
)
if /i "%MODE%"=="release" (
  call :build_one Release
  if errorlevel 1 set "RC=1"
)
if /i "%MODE%"=="all" (
  call :build_one Debug
  if errorlevel 1 set "RC=1"
  call :build_one Release
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
rem %1 = Debug | Release
:build_one
set "TGT=%~1"
if not exist "build_mdk\%TGT%" mkdir "build_mdk\%TGT%"

echo [UV4  ] target %TGT%
UV4.exe -b stm32f429_touchpad.uvprojx -t %TGT% -j0 -o "build_mdk\%TGT%\uv4.log"
set "UVR=%ERRORLEVEL%"

if not exist "build_mdk\%TGT%\uv4.log" (
  echo [FAIL ] UV4 produced no log, exit code %UVR%
  exit /b 1
)

findstr /c:"error:" /c:"Error:" "build_mdk\%TGT%\uv4.log" >nul 2>nul
if not errorlevel 1 (
  echo -------- first errors --------
  findstr /c:"error:" /c:"Error:" "build_mdk\%TGT%\uv4.log"
  echo -----------------------------
)

echo -------- size (%TGT%) --------
where python >nul 2>nul
if not errorlevel 1 (
  python tools\mdk_size.py "build_mdk\%TGT%"
) else (
  findstr /c:"Program Size:" "build_mdk\%TGT%\uv4.log"
)
echo -----------------------------
findstr /c:"- 0 Error" "build_mdk\%TGT%\uv4.log"

if "%UVR%"=="0" (
  echo [ OK  ] %TGT%: 0 errors, 0 warnings
  exit /b 0
)
if "%UVR%"=="1" (
  echo [FAIL ] %TGT%: warnings present - zero-warning policy, see build_mdk\%TGT%\uv4.log
  exit /b 1
)
if "%UVR%"=="2" (
  echo [FAIL ] %TGT%: compile or link errors
  exit /b 1
)
if "%UVR%"=="3" (
  echo [FAIL ] %TGT%: fatal error
  exit /b 1
)
if "%UVR%"=="11" (
  echo [FAIL ] %TGT%: UV4 cannot open the project file
  exit /b 1
)
if "%UVR%"=="12" (
  echo [FAIL ] %TGT%: device not found - install Keil.STM32F4xx_DFP
  exit /b 1
)
echo [FAIL ] %TGT%: UV4 exit code %UVR%
exit /b 1

rem ------------------------------------------------------------ regenerate ----
:regen
where python >nul 2>nul
if errorlevel 1 (
  echo [FAIL] python not found in PATH, cannot regenerate the project
  exit /b 1
)
python tools\gen_keil_project.py
exit /b 0

:regen_only
call :regen
exit /b %ERRORLEVEL%

rem ---------------------------------------------------------------- clean ----
:clean
if exist build_mdk rmdir /s /q build_mdk
echo [ OK  ] removed build_mdk\
exit /b 0

rem ---------------------------------------------------------------- usage ----
:usage
echo.
echo   build_mdk.bat            Debug build       (build_mdk\Debug\)
echo   build_mdk.bat release    Release build     (build_mdk\Release\)
echo   build_mdk.bat all        both
echo   build_mdk.bat gen        regenerate the .uvprojx file list
echo   build_mdk.bat clean      wipe build_mdk\
echo   build_mdk.bat all gen    regenerate, then build both
echo.
echo   Toolchain: UV4.exe in PATH, Keil.STM32F4xx_DFP installed.
echo.
exit /b 2
