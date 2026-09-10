---
name: esp-idf-whole-archive-link
description: ESP-IDF 链接陷阱：组件编译成 .a 后，未被直接引用的目标文件（如 tinyusb 的 tud_descriptor_* 回调）会被静态链接器丢弃导致 undefined reference；同时强定义的 __weak 覆盖会被 tinyusb 的弱桩静默取代，造成运行时异常。用 --whole-archive 包裹组件归档，让所有目标文件无条件链接，彻底消除这两类问题。适用于 ESP-IDF + tinyusb，或任何提供回调 / 弱符号覆盖的自定义组件。
---

# ESP-IDF 组件链接：用 --whole-archive 避免 .a 丢弃与 __weak 误链

## 问题现象
- 链接报错 `undefined reference to tud_descriptor_device_cb / configuration_cb / string_cb / tud_hid_descriptor_report_cb`
  （tinyusb 要求应用提供这些回调，但提供它们的 .c 目标文件被静态归档丢弃）。
- 或：运行时行为异常（如 HID 收不到主机报告），因为 tinyusb 的 `__weak` 回调桩静默胜出了你组件里
  强定义的覆盖——该覆盖所在的 .obj 因“未被直接引用”被归档丢弃。

## 根因
ESP-IDF 把每个组件编译成静态库 `.a`。链接器只拉取“能解析当前未定义符号”的成员。本组件在链接顺序上
位于 tinyusb 之前（因为它 `REQUIRES` tinyusb），其归档在 tinyusb 尚未产生 `tud_descriptor_*` 未定义
引用前就被扫描，导致 `usb_descriptors.c.obj` 被丢弃。弱符号同理：强覆盖所在目标文件被丢弃 → 弱桩胜出。

## 修复（结构性，非打补丁）
ESP-IDF 组件天生是 `.a`；正确做法是**最终链接时用 `--whole-archive` 包裹自定义组件归档**，让其中每个
目标文件无条件纳入，等价于“全部一起链接，不再按引用挑选”。

推荐放在 `main/CMakeLists.txt`（从 main 引用组件目标，避免组件自链接报错）：
```cmake
idf_component_register(SRCS "app_main.c" INCLUDE_DIRS "." REQUIRES usb_device cmsis_dap)

set(DAP_COMPONENTS usb_device cmsis_dap debug_engine swd target)
foreach(comp ${DAP_COMPONENTS})
    idf_component_get_property(comp_lib ${comp} COMPONENT_LIB)
    if(comp_lib)
        target_link_libraries(${COMPONENT_TARGET} PRIVATE
            "-Wl,--whole-archive" "$<TARGET_FILE:${comp_lib}>" "-Wl,--no-whole-archive")
    endif()
endforeach()
```
要点：
- 从 `main` 引用组件目标（不是组件自引用），避开 CMake 自链接报错。
- `$<TARGET_FILE:...>` 生成器在链接期解析绝对路径，链接器直接打开 `.a`，无需 `-L`。
- ESP-IDF 已按 REQUIRES 把组件 `.a` 加入链接行；这里再用 whole-archive 包一层，同一归档去重无冲突，
  只是把所有成员纳入。

## 不要做的事
- 不要用“在已拉取目标文件里加 dummy 引用强制拉取另一目标文件”的打补丁方式——能 work 但丑且脆弱，
  whole-archive 才是结构性正解（用户也明确要求“不要编译成 .a 形式、最终一起全部链接”）。
- 不要试图让 ESP-IDF 不生成 `.a`（组件模型固有）；whole-archive 是等价且受支持的做法。

## 验证
构建后用工具链 nm 检查最终 elf：
```bash
xtensa-esp32s3-elf-nm -C build/esp32s3_debug_probe.elf | grep -iE "tud_descriptor_device_cb|tud_hid_set_report_cb"
```
期望这些是 `T`（强定义、已链接），而非缺失或被 `W`（弱桩）取代。仍存在的 `W` 符号若是你确实未实现、
可接受用 tinyusb 默认桩的回调（如 tud_suspend_cb / tud_resume_cb / tud_sof_cb），不是问题。

## 构建命令（本项目）
```bash
cd <proj> && source env.sh && "$PY" idf_runner.py build
```
注意：完整 clean 重建（约 756 步）可能超过 ~7 分钟沙箱超时被 SIGTERM 打断在最终 app 链接；
增量重建能正常完成。若被打断，重跑一次 `build` 即可（仅剩最终链接）。

---

## 配套：一键编译 / 一键下载脚本（Windows + Git Bash）

ESP-IDF 在本机（Windows + MSYS Git Bash）有三个坑，脚本要一并解决：
1. `activate.ps1` / 裸 `idf.py` 在 Git Bash 下报 "Support for platform 'Windows-'" 或找不到 python。
2. Git Bash 注入 `MSYSTEM=MINGW64`，会让 idf.py 走 MSYS 路径解析而告警/出错。
3. 沙箱缺 `PROCESSOR_ARCHITECTURE` / `ESP_IDF_VERSION` 等环境变量，需手动补。

### env.sh（工程根目录，手动激活环境）
```bash
export IDF_TOOLS_PATH='C:/Espressif/tools'
export IDF_PATH='D:/data/agent-tools/esp32/v6.1/esp-idf'
export ESP_ROM_ELF_DIR='C:/Espressif/tools/esp-rom-elfs/20241011/'
export IDF_PYTHON_ENV_PATH='C:/Espressif/tools/python/v6.1/venv'
export IDF_CCACHE_ENABLE=1
export IDF_COMPONENT_STORAGE_URL="file://C:/Espressif/tools"
export PATH="/c/Espressif/tools/python/v6.1/venv/Scripts:/c/Espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin:/c/Espressif/tools/cmake/4.0.3/bin:/c/Espressif/tools/ninja/1.12.1:/c/Espressif/tools/ccache/4.12.1:$PATH"
PY="/c/Espressif/tools/python/v6.1/venv/Scripts/python.exe"
unset MSYSTEM
export PROCESSOR_ARCHITECTURE=AMD64
export ESP_IDF_VERSION=6.1
```
（路径随本机 ESP-IDF 安装位置调整；`IDF_PATH` 指向离线 esp-idf 目录。）

### idf_runner.py（用 runpy 剥 MSYSTEM 后调用 idf.py）
```python
import os, sys, runpy
os.environ.pop('MSYSTEM', None)
tools_dir = os.path.join(os.environ['IDF_PATH'], 'tools')
sys.path.insert(0, tools_dir)
sys.argv = ['idf.py'] + sys.argv[1:]
runpy.run_path(os.path.join(tools_dir, 'idf.py'), run_name='__main__')
```

### build.sh（一键编译）
```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
source ./env.sh
echo "==> 编译工程: esp32s3_debug_probe"
"$PY" idf_runner.py build "$@"
echo "==> 编译完成"
```

### flash.sh [PORT] [BAUD]（一键下载，扫描系统串口）
核心：用 IDF 自带 python（含 pyserial）扫描 COM 端口；默认端口 COM21，若在列表中自动选用，
交互终端下可手动选；端口列表打印到 stderr（终端可见），选定端口作为 stdout 末行供 `tail -1` 解析。
```bash
#!/usr/bin/env bash
cd "$(dirname "$0")"; source ./env.sh
DEFAULT_PORT="COM21"; ARG_PORT="$1"; ARG_BAUD="$2"
CHOSEN="$( "$PY" - "$ARG_PORT" "$DEFAULT_PORT" <<'PYEOF'
import sys, os
arg_port=sys.argv[1] or ""; default_p=sys.argv[2]
try:
    import serial.tools.list_ports as lp
    ports=[p.device for p in lp.comports()]; descs={p.device:p.description for p in lp.comports()}
except Exception as e:
    sys.stderr.write("ERR: 端口扫描失败: %s\n"%e); sys.exit(2)
if not ports:
    sys.stderr.write("ERR: 未检测到任何串口设备\n"); sys.exit(3)
sys.stderr.write("检测到以下串口:\n")
for d in ports:
    sys.stderr.write("  %-8s | %s%s\n"%(d,descs.get(d,""),"  (默认)" if d==default_p else ""))
chosen=None
if arg_port:
    chosen=arg_port if arg_port in ports else (sys.stderr.write("WARN: %s 不在列表, 仍尝试\n"%arg_port) or arg_port)
elif default_p in ports:
    chosen=default_p
elif os.isatty(0):
    try: sel=input("请选择端口 [回车=默认 %s]: "%default_p)
    except EOFError: sel=""
    sel=sel.strip()
    chosen=default_p if sel=="" else ports[int(sel)-1]
else:
    chosen=default_p if default_p in ports else ports[0]
print(chosen)
PYEOF
)" || { echo "端口选择失败" >&2; exit 1; }
CHOSEN_PORT="$(printf '%s\n' "$CHOSEN" | tail -1)"
echo "==> 烧录端口: $CHOSEN_PORT  波特率: ${ARG_BAUD:-460800}"
"$PY" idf_runner.py flash -p "$CHOSEN_PORT" -b "${ARG_BAUD:-460800}"
echo "==> 烧录完成"
```
要点：
- 串口端口号会变（如 COM1 通信端口 / COM21 CH343 下载口 / COM22），**动态扫描**避免硬编码。
- 不传 PORT 时默认 COM21；传 `./flash.sh COM21` 则直接用指定口（可带波特率）。
- `idf.py flash` 会自动构建缺失产物，已构建则直接烧录；三段（bootloader/partition-table/app）
  烧完各自 `Hash of data verified`，最后 `Hard resetting via RTS pin... Done`。
- **不要**走 `activate.ps1` 或裸 `idf.py`，也**不要**在 Git Bash 里直接 `./flash.sh` 之外再套一层 cmd。

