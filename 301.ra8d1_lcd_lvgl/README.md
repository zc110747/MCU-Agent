# 301.ra8d1_lcd_lvgl — RA8D1 Vision Board (RT-Thread Nano + LVGL)

基于 **Renesas RA8D1 Vision Board**（Cortex-M85 @480MHz）的干净嵌入式工程：
Arm GNU GCC 15.3.1 + CMake/Ninja + OpenOCD + CMSIS-DAP(ART-Link)，CLI 可构建、可烧录、可调试。

## 硬件速览

- MCU：R7FA8D1BH（Cortex-M85，480MHz，2MB Flash，1MB SRAM，64KB ITCM/DTCM）
- SDRAM 32MB @0x68000000；QSPI Flash 1MB @0x60000000
- Debug UART：UART9 @ P208/P209，115200 8N1；LED：P102
- LCD：RGB 4.3" 800×480 RGB565（GLCDC，framebuffer 在 SDRAM）/ MIPI-DSI 可选
- Camera：CEU 8-bit DVP + SCCB；Touch：GT9147

详见 `documents/`（hardware / pinmap / memory-map / architecture / phase0-report）。

## 目录

```text
applications/   应用层
bsp/            板级适配层（startup/linker/uart/lcd/camera/lvgl port）
Drivers/        Renesas FSP/HAL 原始驱动（从官方 BSP 拷贝，保持原样）
third_party/    rt-thread-nano、lvgl（上游原样）
documents/      分析文档与阶段验收报告
tools/          build/flash CLI 脚本 + FLM / pack 下载器
.vscode/        VSCode 构建/仿真任务与 cortex-debug 配置
pyocd.yaml      pyOCD 会话配置（generic cortex_m target，SWD 1MHz）
pyocd_user.py   pyOCD user script：注入 RA8D1 memory map + FLM 闪存算法
openocd.cfg     OpenOCD 备用调试链（Cortex-M85 / CMSIS-DAP，gdb :3333）
R7FA8D1BH.svd   外设寄存器视图（从 Renesas RA DFP pack 提取）
```

依赖方向：`applications → bsp → { Drivers, third_party }`，禁止反向。

## 阶段状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| Phase 0 | 硬件与官方 BSP 分析 | ✅ |
| Phase 0.5 | GCC 裸机最小工程 + OpenOCD 调试链 | ✅ |
| Phase 0.6 | VSCode 在线仿真（pyOCD + cortex-debug） | ✅ |
| Phase 1 | RT-Thread Nano + LED + UART | ⏳ |
| Phase 2 | LCD（GLCDC，无 LVGL） | ⏳ |
| Phase 3 | Camera（CEU） | ⏳ |
| Phase 4 | LVGL v9.1.0 | ⏳ |

## VSCode 在线仿真（pyOCD + cortex-debug）

`F5` → 选 **Debug (pyOCD + ART-Link)**：自动 `build` → gdb load 烧录 → 停在 `entry()` 断点行，
可单步 / 看变量 / 看外设寄存器（SVD）。**Attach (no reflash)** 用于接管板上已运行的程序。

- 任务（Ctrl+Shift+B）：`configure` / `build` / `clean` / `flash`（flash 先 build 再烧录）
- 工具全部走系统 PATH 裸程序名：`cmake` / `ninja` / `arm-none-eabi-gdb` / `pyocd` / `openocd`

### 为什么不是 pack target / OpenOCD

| 项 | 结论 |
|---|---|
| DFP pack target `R7FA8D1BH` | ❌ 在 ART-Link 上 init 失败 —— 用 `target_override: cortex_m` + FLM 注入 |
| pyOCD 闪存算法 | ✅ 官方 `RA8D1_2M.FLM`，由 `pyocd_user.py` 注入 memory map，`create_flash` 自动加载 |
| OpenOCD 0.12 | ✅ 能连（识别 Cortex-M85 r0p2 / 8 断点），但无 RA8D1 flash bank → 只能调试不能烧录 |
| 入口符号 | ⚠️ 是 `entry()` 不是 `main()`（`runToEntryPoint: entry`） |
| 调试信息 | ⚠️ 只有 Debug 构建带 `-g`；Release 无 DWARF，源码级断点失效 |
| pyOCD 安装位置 | ⚠️ 必须在系统 PATH：它装在 venv 时 VSCode `spawn pyocd` 报 ENOENT（已装到系统 Python） |
| cortex-debug 启动检测 | ⚠️ 内置等待正则 `GDB server started (at\|on) port` 与 pyocd 0.45 的 `GDB server listening on port` 不匹配 → 报 `Timeout.`；用 `overrideGDBServerStartedRegex` 修正 |
| gdb `load`（F5 烧录） | ⚠️ 两个前提：① `flash.timeout.*` 放宽（默认 10s 会 `erase sector timed out`）；② option-setting 段改 `(NOLOAD)`，否则 load 整段中止 |

### CLI 等价命令

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   rem configure
cmake --build build                                     rem build
pyocd flash -t cortex_m -O connect_mode=pre-reset build\firmware.elf   rem 烧录
tools\flash\flash.bat                                   rem 备用烧录（python API 直连）
openocd -f openocd.cfg                                  rem 备用调试链（gdb :3333）
```

## 实测数据（Phase 0.6，ART-Link 探针在线）

- 双构零警告：Debug FLASH 6120B(0.29%) / RAM 24736B(2.36%)；Release FLASH 4996B(0.24%) / RAM 24740B(2.36%)
- pyOCD 烧录：`Erased 8192B (1 sector), programmed 8192B (1 page) @21.83 kB/s`（FLM 生效）
- pyOCD 连接：`Cortex-M85 r0p2 / v8.1-M`，DPIDR 0x6ba02477，8 硬件断点 + 8 watchpoint
- OpenOCD 连接：`Cortex-M85 r0p2 processor detected`，`reset halt` 后 PC=0x020001cc / MSP=0x220060a8
- 硬件断点命中：openocd `bp 0x02000110 2 hw` + `resume` → `halted due to breakpoint, pc: 0x02000110`（= `entry()`）
- **F5 全链路（gdb load 经 RSP）**：`Loading section .text/.data` → `Transfer rate: 13 KB/sec` →
  复位 → `break entry` + `continue` → **命中 `entry()` at `main.c:15`**，栈帧 `Reset_Handler (startup.c:71)`，
  单步进入 `bsp_led_init (bsp_led.c:22)`，PC=0x02000214
- 双构零警告（改 linker 后复测）：Debug FLASH 6120B(0.29%) / RAM 24736B(2.36%)；Release FLASH 4996B(0.24%) / RAM 24740B(2.36%)

> 烧录时 `Failed to add data chunk: 0x0300a100` 属 option-setting 区（elf 含该 section，pyOCD/flash.py
> 均未定义其 region），与 flash.py 行为一致，不影响运行。

- 编译参数：`-mcpu=cortex-m85 -mfpu=auto -mfloat-abi=hard -mthumb`

## 参考

官方 BSP（只读参考，不参与构建）：`E:/cnb/git/sdk-bsp-ra8d1-vision-board-master`
（https://github.com/RT-Thread-Studio/sdk-bsp-ra8d1-vision-board）
