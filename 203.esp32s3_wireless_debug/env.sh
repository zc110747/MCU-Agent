# ESP-IDF v6.1 environment for Git Bash (manual activation; activate.py fails under MSYS)
export IDF_TOOLS_PATH='C:/Espressif/tools'
export IDF_PATH='D:/data/agent-tools/esp32/v6.1/esp-idf'
export ESP_ROM_ELF_DIR='C:/Espressif/tools/esp-rom-elfs/20241011/'
export IDF_PYTHON_ENV_PATH='C:/Espressif/tools/python/v6.1/venv'
export IDF_CCACHE_ENABLE=1
export IDF_COMPONENT_STORAGE_URL="file://C:/Espressif/tools"
export PATH="/c/Espressif/tools/python/v6.1/venv/Scripts:/c/Espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:/c/Espressif/tools/cmake/4.0.3/bin:/c/Espressif/tools/ninja/1.12.1:/c/Espressif/tools/ccache/4.12.1:$PATH"
alias idf.py="python -m idf_py" 2>/dev/null
PY="/c/Espressif/tools/python/v6.1/venv/Scripts/python.exe"
idf() { "$PY" "$IDF_PATH/tools/idf.py" "$@"; }
unset MSYSTEM
export PROCESSOR_ARCHITECTURE=AMD64
export ESP_IDF_VERSION=6.1
export ESP_IDF_PYTHON_ENV_PATH="C://Espressif//tools//python//v6.1//venv"
