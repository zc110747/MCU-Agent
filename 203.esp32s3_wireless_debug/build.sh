#!/usr/bin/env bash
# ============================================================================
# 一键编译脚本 (ESP-IDF v6.1 / Git Bash)
#   用法:  ./build.sh            -> 增量编译
#         ./build.sh clean       -> 清空前重新编译
#         ./build.sh <idf参数>   -> 透传给 idf.py build
#   说明:  通过 idf_runner.py 调用 idf.py，已处理 Git Bash/MSYSTEM 兼容问题。
# ============================================================================
set -e

# 切换到脚本所在目录，保证相对路径正确
cd "$(dirname "$0")"

# 载入 ESP-IDF 环境 (env.sh 设置 IDF_PATH / PY / PATH 等)
if [ ! -f ./env.sh ]; then
    echo "ERR: 未找到 env.sh (请确认在工程根目录运行)" >&2
    exit 1
fi
source ./env.sh

echo "==> 编译工程: esp32s3_debug_probe"
echo "==> IDF_PATH = $IDF_PATH"
echo "==> Python  = $PY"

# 把脚本后面的所有参数透传给 idf.py build
"$PY" idf_runner.py build "$@"

echo "==> 编译完成 (build/esp32s3_debug_probe.bin)"
