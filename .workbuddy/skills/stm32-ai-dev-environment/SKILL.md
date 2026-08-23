---
name: stm32-ai-dev-environment
description: STM32 AI Agent 开发环境验证与 Windows/GitBash 踩坑：arm-none-eabi-gcc、cmake、ninja、openocd、python 的安装与 PATH 配置；Zephyr 的 west 调用方式；Git Bash 吞花括号、中文 GBK 编码、openocd swd 传输等平台坑。适用于"配置 AI 能直接构建的嵌入式环境""排查 Agent 构建失败""Windows 下交叉编译环境搭建""west 不在 PATH 怎么调"。触发词：AI 开发环境、工具链安装、arm-none-eabi-gcc、cmake ninja、openocd、west 调用、Git Bash 坑、GBK 编码、嵌入式环境验证、STM32 环境配置。
agent_created: true
---

# STM32 AI 开发环境验证与 Windows 坑

让 AI Agent 能在 Windows 下**直接构建、烧录、仿真**的前提：工具链全部进 PATH，且
避开 Windows/GitBash 的几处经典坑。本 skill 是 `stm32-vibe-coding-workflow` 的环境落地篇。

## 一、必装工具（全部进系统 PATH，命令行可直接调用）

| 工具 | 用途 | 下载/安装 |
|---|---|---|
| `arm-none-eabi-gcc` ≥13 | 交叉编译器（本机验证 15.3.1） | ARM 官方 GNU toolchain releases |
| `cmake` ≥3.20 | 构建系统 | MSYS2：`pacman -S mingw-w64-ucrt-x86_64-cmake` |
| `ninja` | 构建后端 | MSYS2：`pacman -S mingw-w64-ucrt-x86_64-ninja` |
| `openocd` 0.12.0 | 烧录/调试 | SourceForge openocd 0.12.0-rc1 |
| `python` | 生成测试代码、跑 verify 脚本 | python.org |
| `gcc` (MSYS2 ucrt) | 编译 PC 侧测试桩（如 shell 逻辑单测） | `pacman -S mingw-w64-ucrt-x86_64-gcc` |

**VSCode + Cortex-Debug** 扩展也要装（单步仿真 F5 一键编译调试）。

⚠️ 驱动需人自行安装：ST-Link / J-Link / USB 转串口（CP210x 等），Agent 装不了。

## 二、环境自检（让 Agent 跑一遍确认）

```bash
arm-none-eabi-gcc --version
cmake --version
ninja --version
openocd --version
python --version
```
任一命令 `command not found` → PATH 没配好，先解决再继续。

## 三、Zephyr 工程的 west 调用

本机 `west.exe` **不在 PATH**，不能直接 `west build`。必须走 python 模块：
```bash
python -m west build -b nucleo_h743zi/stm32h743xx -d build -s .
```
每次新 shell 需注入：
```bash
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=<arm-none-eabi 安装路径>
export PATH="$GNUARMEMB_TOOLCHAIN_PATH/bin:$PATH"
```
- `ZEPHYR_BASE` 优先取环境变量（west 自动注入）；未设置时回退到工程内 `zephyr/zephyr`。
- 若在 MSYS2/Git Bash 手动 export `ZEPHYR_BASE`，**务必用 Windows 路径**（`D:/...`）
  而非 POSIX（`/d/...`），否则原生 cmake 无法解析。

## 四、Windows / Git Bash 平台坑（高频）

### 4.1 Git Bash 吞掉 curl 的 `%{...}` 花括号
`-w "%{http_code}"` 被 bash 当变量展开 → 输出 `code=%http_coden` → 压测全判失败，
害你去查固件。改用响应体内容判定：
```bash
curl -s --max-time 5 http://IP/ | grep -q "<页面特征串>"
```

### 4.2 中文输出需转码
`ipconfig` / `ping` 等输出 GBK 编码，python 解析前先转：
```bash
ping -n 3 -w 1500 -l $sz <IP> 2>&1 | iconv -f GBK -t UTF-8 | grep -c "字节=$sz"
```

### 4.3 openocd 0.12 用 `transport select swd`
**不支持旧 `hla_swd`**（会报 "adapter doesn't support"）。所有 cfg 用：
```tcl
transport select swd
```

### 4.4 原生 Windows 工具链看不到 MSYS 的 `/tmp`、`/dev/null`
探测文件（如宏体检 probe.c）放**项目目录内**，不要放 `/tmp`。

### 4.5 `#pragma message` 在 `-E` 下不求值
想确认宏实际生效值，必须用 `gcc -dM -E`（不是 `#pragma message`）：
```bash
arm-none-eabi-gcc <所有 -D 和 -I 原样> -dM -E build/probe.c -o build/probe.i
grep -E "define (IP_REASSEMBLY|CHECKSUM_)" build/probe.i
```

### 4.6 arp 核对 MAC OUI 防假通
`arp -a <IP>` 确认应答者厂商（ST 是 `00:80:E1`），排除同 IP 其它设备顶替造成的假通。

## 五、编译期零警告约束

本项目强约束 **Debug / Release 双构零警告**：
```cmake
add_compile_options(-Wall -Wextra)
```
- 链接脚本 `.ld` 改动后必须让 CMake 跟踪（见 `stm32-project-scaffold` 的 LINK_DEPENDS）。
- `-specs=nano.specs` 默认不链浮点 printf，需加 `-u _printf_float`（否则 MPU9250 九轴显示空白）。
- mbedTLS 3.x 的 `-Warray-bounds` 假阳性（common.h 128 位 union），在 config 头
  `#pragma GCC diagnostic ignored "-Warray-bounds"` 屏蔽。
