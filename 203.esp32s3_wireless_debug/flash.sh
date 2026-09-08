#!/usr/bin/env bash
# ============================================================================
# 一键下载/烧录脚本 (ESP-IDF v6.1 / Git Bash)
#   用法:
#     ./flash.sh              -> 扫描系统串口, 默认选 COM21 (若在列表中) 自动烧录
#     ./flash.sh COM21        -> 直接烧录到指定端口 COM21
#     ./flash.sh COM21 921600 -> 指定端口 + 波特率
#   特性:
#     - 自动扫描系统所有 COM 端口并打印
#     - 默认端口 COM21, 若未指定且 COM21 存在则自动选用
#     - 交互终端下可手动选择端口 (非交互环境自动回退默认端口)
#     - 烧录前校验端口存在, 失败给出明确提示
# ============================================================================

# 切换到脚本所在目录
cd "$(dirname "$0")"

if [ ! -f ./env.sh ]; then
    echo "ERR: 未找到 env.sh (请确认在工程根目录运行)" >&2
    exit 1
fi
source ./env.sh

DEFAULT_PORT="COM21"
ARG_PORT="$1"
ARG_BAUD="$2"

# ---------------------------------------------------------------------------
# 用 IDF 自带的 python (含 pyserial) 扫描端口并选定目标端口
#   - 打印端口列表到 stdout
#   - 最后一行输出选定端口 (供 bash 解析)
# ---------------------------------------------------------------------------
SCAN_OUT="$("$PY" - "$ARG_PORT" "$DEFAULT_PORT" <<'PYEOF'
import sys, os
arg_port  = sys.argv[1] or ""
default_p = sys.argv[2]
try:
    import serial.tools.list_ports as lp
    ports = [p.device for p in lp.comports()]
    descs = {p.device: p.description for p in lp.comports()}
except Exception as e:
    sys.stderr.write("ERR: 端口扫描失败: %s\n" % e)
    sys.exit(2)

if not ports:
    sys.stderr.write("ERR: 未检测到任何串口设备\n")
    sys.exit(3)

# 端口列表打印到 stderr, 使其在终端可见 (stdout 末尾仅保留选定端口供 bash 解析)
sys.stderr.write("检测到以下串口:\n")
for d in ports:
    mark = "  (默认)" if d == default_p else ""
    sys.stderr.write("  %-8s | %s%s\n" % (d, descs.get(d, ""), mark))

# 决定端口
chosen = None
if arg_port:
    if arg_port in ports:
        chosen = arg_port
    else:
        sys.stderr.write("WARN: 指定端口 %s 不在列表中, 仍尝试使用该端口\n" % arg_port)
        chosen = arg_port
else:
    if default_p in ports:
        chosen = default_p
    elif os.isatty(0):
        try:
            sel = input("请选择端口 [回车=默认 %s]: " % default_p)
        except EOFError:
            sel = ""
        sel = sel.strip()
        if sel == "":
            chosen = default_p if default_p in ports else ports[0]
        else:
            try:
                idx = int(sel) - 1
                chosen = ports[idx]
            except (ValueError, IndexError):
                sys.stderr.write("ERR: 无效选择: %s\n" % sel)
                sys.exit(4)
    else:
        # 非交互环境: 回退默认端口 (若在列表) 否则第一个
        chosen = default_p if default_p in ports else ports[0]

print(chosen)
PYEOF
)" || { echo "端口选择失败, 中止烧录" >&2; exit 1; }

# 取最后一行作为选定端口
CHOSEN_PORT="$(printf '%s\n' "$SCAN_OUT" | tail -1)"
if [ -z "$CHOSEN_PORT" ] || [ "${CHOSEN_PORT#ERR:}" != "$CHOSEN_PORT" ]; then
    echo "ERR: 未获得有效端口: $CHOSEN_PORT" >&2
    exit 1
fi

# 波特率
BAUD="${ARG_BAUD:-460800}"

echo "==> 烧录端口 : $CHOSEN_PORT"
echo "==> 波特率   : $BAUD"
echo "==> 目标     : esp32s3_debug_probe"

# 调 idf.py flash (会自动构建缺失产物, 已构建则直接烧录)
"$PY" idf_runner.py flash -p "$CHOSEN_PORT" -b "$BAUD"

echo "==> 烧录完成"
