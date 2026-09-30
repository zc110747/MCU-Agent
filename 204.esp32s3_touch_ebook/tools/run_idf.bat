@echo off
REM ============================================================
REM  INTERNAL helper used by the build agent.
REM      tools\run_idf.bat <idf.py args...>
REM  Runs idf.py with the toolchain activated and mirrors every
REM  line (stdout+stderr, ANSI stripped) into build_log.txt so the
REM  result can be inspected even when the caller swallows output.
REM ============================================================
setlocal
REM The idf.py -> cmake -> esp_idf_size chain round-trips text through the
REM console code page.  On a Chinese Windows install that turns the size
REM report's box-drawing characters into mojibake, so pin the code page to
REM UTF-8 for this console and every child process.
chcp 65001 >nul 2>&1
call "%~dp0idf_env.bat"

REM Export the PATH *cmd* resolved.  The host shell may have injected a second
REM case-variant of PATH that CPython would otherwise prefer; see
REM tools/idf_launcher.py for the full explanation.
set "IDF_LAUNCH_PATH=%PATH%"

set "PROJ=%~dp0.."
cd /d "%PROJ%"
set "LOG=%PROJ%\build_log.txt"

echo === idf.py %* === > "%LOG%"
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%~dp0idf_launcher.py" %* >> "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
echo. >> "%LOG%"
echo === EXIT CODE %RC% === >> "%LOG%"

REM strip ANSI escape sequences so the log is plain text
"%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" "%~dp0strip_ansi.py" "%LOG%"

endlocal & exit /b %RC%
