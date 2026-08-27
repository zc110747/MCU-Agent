#!/usr/bin/env bash
# ============================================================================
# build_all.sh - ESP32-S3 USB Wi-Fi RNDIS 一键完整编译 (Git Bash / Linux)
#
# 前置: ESP-IDF v5.5.5 已安装到 /d/software/esp32-tools/esp-idf
# 用法: bash tools/build_all.sh
# ============================================================================
set -euo pipefail

IDF_PATH="${IDF_PATH:-/d/esp/esp-idf}"
IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "[ERR] 未找到 $IDF_PATH/export.sh"
    echo "[ERR] 请先安装 ESP-IDF 到 /d/software/esp32-tools/esp-idf"
    exit 1
fi

echo "[INFO] IDF_PATH       = $IDF_PATH"
echo "[INFO] IDF_TOOLS_PATH = $IDF_TOOLS_PATH"
echo "[INFO] PROJECT_DIR    = $PROJECT_DIR"

# 加载 ESP-IDF 环境
echo "[INFO] 加载 ESP-IDF 环境 ..."
. "$IDF_PATH/export.sh"

cd "$PROJECT_DIR"

echo "[INFO] 指定目标芯片 esp32s3 ..."
idf.py set-target esp32s3

echo "[INFO] 完整编译中 ..."
idf.py build

echo "[OK] 编译完成，产物: build/esp32s3/esp32s3.bin / partitions.bin / bootloader.bin"
