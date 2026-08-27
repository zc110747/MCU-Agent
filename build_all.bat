@echo off
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

REM --- counters ---
set "PASS=0"
set "FAIL=0"
set "SKIP=0"

REM --- project list in build order ---
set "PROJ_LIST=001.stm32h743_tinyusb_cdc_msc 002.stm32h743_tinyusb_uvc_ov5640 003.stm32h743_lvgl_oled 004.stm32h743_sd_oled_img 005.stm32h743_person_detect 006.stm32h743_face_detect 007.stm32h743_cmsis_dap 008.stm32h743_lvgl_mos 009.stm32h743_zephyr 010.stm32h743_boot 101.stm32f429_net 102.stm32f429_tinyusb_ui 201.esp32s3_rtos 202.esp32s3_usb_wifi"

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
