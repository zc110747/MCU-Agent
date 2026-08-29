---
name: stm32-ai-dev-environment
description: STM32 AI Agent 开发环境验证与 Windows/GitBash 踩坑：arm-none-eabi-gcc、cmake、ninja、openocd、python 的安装与 PATH 配置；Zephyr 的 west 调用方式；Git Bash 吞花括号、中文 GBK 编码、openocd swd 传输等平台坑。适用于"配置 AI 能直接构建的嵌入式环境""排查 Agent 构建失败""Windows 下交叉编译环境搭建""west 不在 PATH 怎么调"。触发词：AI 开发环境、工具链安装、arm-none-eabi-gcc、cmake ninja、openocd、west 调用、Git Bash 坑、GBK 编码、嵌入式环境验证、STM32 环境配置、双 openocd 端口冲突、PowerShell stderr 误报红色错误、LIBUSB_ERROR_ACCESS、ST-Link 被残留进程占用。
agent_created: true
---

# STM32 AI 开发环境验证与 Windows 坑

让 AI Agent 能在 Windows 下**直接构建、烧录、仿真**的前提：工具链全部进 PATH，且
避开 Windows/GitBash 的几处经典坑。本 skill 是 `stm32-vibe-coding-workflow` 的环境落地篇。

## 一、必装工具（全部进系统 PATH，命令行可直接调用）

| 工具 | 用途 | 下载/安装 |
|---|---|---|
| `arm-none-eabi-gcc` ≥13 | 交叉编译器（建议 ≥13，已验证 15.3.1 可用） | ARM 官方 GNU toolchain releases |
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

若 `west.exe` **不在 PATH**，不能直接 `west build`。必须走 python 模块：
```bash
python -m west build -b <board>/<soc> -d build -s .
```
每次新 shell 需注入：
```bash
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=<arm-none-eabi 安装路径>
export PATH="$GNUARMEMB_TOOLCHAIN_PATH/bin:$PATH"
```
- `ZEPHYR_BASE` 优先取环境变量（west 自动注入）；未设置时回退到工程内 `zephyr/zephyr`。
- 若在 MSYS2/Git Bash 手动 export `ZEPHYR_BASE`，**务必用 Windows 风格绝对路径**（如 `C:/...`）
  而非 POSIX 风格（`/c/...`），否则原生 cmake 无法解析。

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

### 4.7 ST-Link 被残留进程占用排查
烧录/调试偶发 `ST-Link not found` / `init mode failed`，多因上一次 `openocd` / `arm-none-eabi-gdb`
未退出仍独占 ST-Link。排查：
```bash
tasklist | findstr openocd
tasklist | findstr arm-none-eabi-gdb
taskkill /F /PID <pid>     # 结束残留再重试
```
推荐常驻一个 openocd 调试服务器（见 `stm32-verification-acceptance` 第八节），避免重复实例抢 ST-Link。

### 4.7.1 双 openocd 实例端口冲突（Git Bash 看不到进程）
若 GDB 报 `Target not examined yet / refuse gdb connection`，先查是否有两个 `openocd.exe` 都绑了
`3333/4444`。**Git Bash 的 `ps` 看不到 Windows 原生进程**，必须用：
```bash
tasklist | findstr openocd
netstat -ano | findstr :3333
# 拿到 PID 后
taskkill /F /PID <pid>
```
最后只起一个 OpenOCD（或复用常驻服务器），端口冲突即解。

### 4.8 PowerShell 把 stderr 包装成「红色报错」的误报
`west build` / `cmake` 等把进度/警告打到 **stderr**，PowerShell 会将其包装成
`RemoteException` / `NativeCommandError`（红色报错外观），但**实际 `BUILD_EXIT=0`、elf 正常生成**
——属 PowerShell 的 stderr→error 误报，并非真失败。
- 判定真失败以**退出码**为准：脚本里 `echo %ERRORLEVEL%` / `exit /b %ERR%`，看 `BUILD_EXIT`，别被红色吓到。
- 在 `.bat` 里把 `pause` 改成 `exit /b %ERR%`，把阻塞 `pause` 去掉，便于 PowerShell/CI 拿到真实错误码（阻塞 `pause` 在 PowerShell 下会卡死或返回 255）。
- 想在 PowerShell 看真实输出，用 `python -m west build ... 2>&1 | Tee-Object -Variable out` 后查 `BUILD_EXIT`。

## 五、编译期零警告约束

本项目强约束 **Debug / Release 双构零警告**：
```cmake
add_compile_options(-Wall -Wextra)
```
- 链接脚本 `.ld` 改动后必须让 CMake 跟踪（见 `stm32-project-scaffold` 的 LINK_DEPENDS）。
- `-specs=nano.specs` 默认不链浮点 printf，需加 `-u _printf_float`（否则含浮点打印时显示空白，例如 IMU 九轴数据）。
- mbedTLS 3.x 的 `-Warray-bounds` 假阳性（common.h 128 位 union），在 config 头
  `#pragma GCC diagnostic ignored "-Warray-bounds"` 屏蔽。

## 六、预编译厂商库与工具链版本锁定（binutils 2.44 坑）

部分厂商**预编译静态库**（如 STemWin 的 `STemWin_CM7_wc16.a`，由 ARM Compiler/armcc 构建）
在 **GNU ld 2.44（随 arm-none-eabi-gcc 15.x 发布）** 下链接会直接中止：

```
(.text.xxx+0x..): undefined reference to `GUI_xxx'
(GUI_xxx): Unknown destination type (ARM/Thumb)
dangerous relocation: unsupported relocation
```

**根因**：库目标文件缺 `.type %function` / 映射符号相关的严格性检查，binutils 2.44 收紧后
拒绝 interworking 重定位。给 `.a` 补 `$t` 映射符号**无法绕过** 2.44 的严格性。

**已验证修复**：把工具链锁到 **binutils < 2.44** 的版本（GNU Arm Embedded
**13.3.rel1 / 14.2.rel1**，binutils 2.43.1）—— 链接即正常。

- 锁定方式（杜绝 PATH 回退到 15.x）：在 `cmake/arm-none-eabi.cmake` 探测目标工具链后，
  把编译器/链接器/ar 等**全部以绝对路径 `FORCE` 写入 CMake 缓存**，并追加 `-B<tc_bin>`
  让 gcc 驱动优先在该 `bin` 解析 `ld`/`as`。
- 经验：凡用到**厂商预编译 `.a`**（emWin / 某些 DSP / 闭源协议栈），先确认其 binutils 兼容性，
  必要时准备一个 binutils <2.44 的降级工具链，不要默认 newest（见 `stm32-peripheral-drivers` 十）。
