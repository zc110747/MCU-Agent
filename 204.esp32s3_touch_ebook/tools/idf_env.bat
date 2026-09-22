@echo off
REM ============================================================
REM  INTERNAL helper - activate the ESP-IDF toolchain environment.
REM  Must be invoked with "call" from another batch file:
REM      call "%~dp0idf_env.bat"
REM  No setlocal/endlocal here on purpose: everything export.bat
REM  sets has to survive into the caller's scope.
REM  Do NOT add PAUSE here: this file is not a user entry point.
REM
REM  IDF_TOOLS_PATH is the *root* of the tool tree, so the per-tool
REM  directories hang directly underneath it (no extra "tools" level).
REM  Override IDF_PATH below if the local SDK moves.
REM ============================================================

if not defined IDF_PATH set "IDF_PATH=D:\data\agent-tools\esp32\v6.1\esp-idf"
if not defined IDF_TOOLS_PATH set "IDF_TOOLS_PATH=C:\Espressif\tools"
if not defined IDF_PYTHON_ENV_PATH set "IDF_PYTHON_ENV_PATH=%IDF_TOOLS_PATH%\python\v6.1\venv"
if not defined ESP_ROM_ELF_DIR set "ESP_ROM_ELF_DIR=%IDF_TOOLS_PATH%\esp-rom-elfs\20241011\"
if not defined IDF_COMPONENT_LOCAL_STORAGE_URL set "IDF_COMPONENT_LOCAL_STORAGE_URL=file://%IDF_TOOLS_PATH%"
set "IDF_CCACHE_ENABLE=1"

REM Force Python's UTF-8 mode for every process in the idf.py -> cmake -> ninja
REM -> esp_idf_size chain.  Without it the chain mixes encodings on a Chinese
REM Windows install: one link writes the box-drawing characters of the size
REM report as UTF-8 while the next decodes them as cp936, and the log ends up
REM full of mojibake (the numbers stay readable, the tables do not).
set "PYTHONUTF8=1"

set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%IDF_PATH%\tools;%IDF_TOOLS_PATH%\idf-exe\1.0.3;%IDF_TOOLS_PATH%\ccache\4.12.1;%IDF_TOOLS_PATH%\ninja\1.12.1;%IDF_TOOLS_PATH%\cmake\4.0.3\bin;%IDF_TOOLS_PATH%\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;%IDF_TOOLS_PATH%\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin;%PATH%"

REM Official activator: exports ESP_IDF_VERSION and the exact tool revisions.
if exist "%IDF_PATH%\export.bat" call "%IDF_PATH%\export.bat" >nul 2>&1

if not defined ESP_IDF_VERSION set "ESP_IDF_VERSION=6.1.0"

exit /b 0
