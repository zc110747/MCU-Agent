@echo off
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "SUPPORT=%ROOT%support_tools"
cd /d "%ROOT%"

echo ============================================================
echo [Support] Sync support packages into all projects
echo ============================================================
echo Support source: %SUPPORT%
echo.

rem Iterate project directories whose names start with 0 or 1.
rem 0x/00x -> stm32h743, 1xx -> stm32f429, name containing zephyr -> zephyr.
rem 2xx ESP32 projects are intentionally not matched here.
for /d %%P in (0* 1*) do (
    call :handle "%%P"
)

echo.
echo [DONE] Support sync complete.
echo Note: 2xx ESP32 projects have no support package and were skipped.
echo.
pause
exit /b 0

:handle
set "PROJ=%~1"
set "PKG="

rem Rule: project name containing zephyr takes precedence.
echo %PROJ% | findstr /i "zephyr" >nul
if not errorlevel 1 set "PKG=zephyr"

if not defined PKG (
    set "PFX=%PROJ:~0,1%"
    if "!PFX!"=="0" set "PKG=h743"
    if "!PFX!"=="1" set "PKG=f429"
)

if not defined PKG (
    echo [SKIP] %PROJ% - no support package mapping
    exit /b 0
)

rem Package directory inside support_tools.
set "PKGDIR=%SUPPORT%\env_support_for_stm32%PKG%"
if "%PKG%"=="zephyr" set "PKGDIR=%SUPPORT%\env_support_for_zephyr"

echo.
echo [PROJECT] %PROJ%  package: %PKG%

rem Step 1: ensure package directory exists; extract zip if missing.
if not exist "%PKGDIR%" (
    echo [EXTRACT] %PKG% package not found, extracting from zip...
    call :extract "%PKG%"
) else (
    echo [OK] %PKG% package already extracted.
)

if not exist "%PKGDIR%" (
    echo [ERROR] %PKG% package directory still missing after extract, skip project.
    exit /b 0
)

rem Step 3: compare package top-level entries with the target project.
for /f "delims=" %%I in ('dir /b "%PKGDIR%"') do (
    if not exist "%ROOT%%PROJ%\%%I" (
        echo [COPY] %PROJ%\%%I - missing, copying...
        robocopy "%PKGDIR%\%%I" "%ROOT%%PROJ%\%%I" /E /NFL /NDL /NJH /NJS /NC >nul 2>&1
    ) else (
        echo [SKIP] %PROJ%\%%I already exists, skip.
    )
)
exit /b 0

:extract
set "P=%~1"
set "ZIP=%SUPPORT%\env_support_for_stm32%P%.zip"
if "%P%"=="zephyr" set "ZIP=%SUPPORT%\env_support_for_zephyr.zip"

echo [EXTRACT] %ZIP%
tar -xf "%ZIP%" -C "%SUPPORT%" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] tar failed, falling back to PowerShell Expand-Archive...
    powershell -Command "Expand-Archive -Path '%ZIP%' -DestinationPath '%SUPPORT%' -Force"
)
exit /b 0
