@echo off
REM ESP-IDF v6.1 environment for native Windows cmd (mirrors env.sh)
REM Keep this file pure ASCII (no Chinese) to avoid GBK console parsing issues.
set "IDF_TOOLS_PATH=C:/Espressif/tools"
set "IDF_PATH=D:\data\agent-tools\esp32\v6.1\esp-idf"
set "ESP_ROM_ELF_DIR=C:/Espressif/tools/esp-rom-elfs/20241011/"
set "IDF_PYTHON_ENV_PATH=C:/Espressif/tools/python/v6.1/venv"
set "IDF_CCACHE_ENABLE=1"
set "IDF_COMPONENT_STORAGE_URL=file://C:/Espressif/tools"
set "PATH=C:/Espressif/tools/python/v6.1/venv/Scripts;C:/Espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin;C:/Espressif/tools/cmake/4.0.3/bin;C:/Espressif/tools/ninja/1.12.1;C:/Espressif/tools/ccache/4.12.1;%PATH%"
set "PY=C:/Espressif/tools/python/v6.1/venv/Scripts/python.exe"
set "PROCESSOR_ARCHITECTURE=AMD64"
set "ESP_IDF_VERSION=6.1"
set "ESP_IDF_PYTHON_ENV_PATH=C:/Espressif/tools/python/v6.1/venv"
REM Clear MSYSTEM if inherited from Git Bash (it breaks idf.py under Windows)
set "MSYSTEM="
