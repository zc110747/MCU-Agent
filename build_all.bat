@echo off
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

REM --- counters ---
set "PASS=0"
set "FAIL=0"
set "SKIP=0"

REM --- project list in build order ---
set "PROJ_LIST="
for /f "usebackq delims=" %%L in ("project.list") do (
  set "PROJ_LIST=!PROJ_LIST! %%L"
)
set "PROJ_LIST=!PROJ_LIST:~1!"
echo %PROJ_LIST%

echo ============================================================
echo  One-Click Build All Projects
echo  Workspace: %ROOT%
echo  Projects : 14
echo ============================================================
echo.

for %%P in (%PROJ_LIST%) do (
    call :build "%%P"
)

echo ============================================================
echo  Build Summary
echo ============================================================
echo  Passed  : !PASS!
echo  Failed  : !FAIL!
echo  Skipped : !SKIP!
echo ============================================================
if !FAIL! gtr 0 (
    echo  Some projects failed - see output above.
) else (
    echo  All projects built successfully.
)
echo.
echo  Press Enter to exit...
pause > nul
goto :eof

:build
set "P=%~1"
echo ============================================================
echo  [Build] %P%
echo ============================================================
if not exist "%ROOT%\%P%\build_oneclick.bat" (
    echo [SKIP] %P% - build_oneclick.bat not found
    set /a "SKIP+=1"
    echo.
    goto :eof
)
REM call child with stdin from nul so its final "pause" is skipped;
REM the parent controls pausing on error only.
call "%ROOT%\%P%\build_oneclick.bat" < nul
if errorlevel 1 (
    echo.
    echo [ERROR] %P% build FAILED - see output above.
    set /a "FAIL+=1"
    echo  Press Enter to continue with remaining projects...
    pause > nul
) else (
    echo.
    echo [OK] %P% build succeeded.
    set /a "PASS+=1"
)
echo.
goto :eof
