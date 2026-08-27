#!/usr/bin/env bash
# ============================================================================
# run.sh - ESP32-S3 USB Wi-Fi RNDIS 一键 编译+烧录+监视 (Git Bash / Linux)
#
# 端口: COM21 (Windows) 或 /dev/ttyUSBx (Linux)；可用 PORT 环境变量覆盖
# 用法: PORT=COM21 bash tools/run.sh
# ============================================================================
set -euo pipefail

PORT="${PORT:-COM21}"
IDF_PATH="${IDF_PATH:-/d/esp/esp-idf}"
IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if [ ! -f "$IDF_PATH/export.sh" ]; then
    echo "[ERR] 未找到 $IDF_PATH/export.sh"
    exit 1
fi

echo "[INFO] 端口 = $PORT"
. "$IDF_PATH/export.sh"
cd "$PROJECT_DIR"

idf.py set-target esp32s3
echo "[INFO] 编译 + 烧录到 $PORT + 监视 ..."
idf.py -p "$PORT" flash monitor
