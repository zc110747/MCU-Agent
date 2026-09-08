@echo off
REM One-click build for ESP32-S3 Debug Probe (native Windows cmd).
REM Usage:  build.bat            (incremental build)
REM          build.bat clean     (clean then build)
REM          build.bat <idf arg> (forwarded to idf.py build)
setlocal
cd /d "%~dp0"
call "%~dp0env.bat"

echo ==^> Building project: esp32s3_debug_probe
if /i "%~1"=="clean" (
    "%PY%" "%~dp0idf_runner.py" clean
    "%PY%" "%~dp0idf_runner.py" build
) else (
    "%PY%" "%~dp0idf_runner.py" build %*
)
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo ==^> Build complete: build/esp32s3_debug_probe.bin
endlocal
