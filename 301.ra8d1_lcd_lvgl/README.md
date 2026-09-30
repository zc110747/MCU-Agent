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
| Phase 1 | RT-Thread Nano + LED + UART | ✅ |
| Phase 2 | LCD（GLCDC，无 LVGL） | ✅ |
| Phase 3 | Camera（CEU） | ✅ |
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

## Phase 1：RT-Thread Nano 5.0.2 + LED + 串口控制台

内核跑在 RT-Thread Nano 上：`entry()` → `rtthread_startup()` → main 线程 → `main()`。
BSP 只提供三个钩子：`rt_hw_board_init()` / `rt_hw_console_output()` / `rt_hw_console_getchar()`。

| 项 | 值 |
|---|---|
| 内核 | RT-Thread Nano 5.0.2（`third_party/rt-thread-nano`，上游原样） |
| 堆 | `RT_USING_SMALL_MEM_AS_HEAP`，64KB 静态数组（`.bss`） |
| tick | SysTick @1kHz（`RT_TICK_PER_SECOND=1000`） |
| 控制台 | SCI9（SCI_B）@P208/P209，115200 8N1；RX 中断 + 环形缓冲 + 信号量，TX 轮询 FIFO |
| Shell | finsh / msh，`tshell` 线程 4096B；命令 `led on|off|blink` |
| 线程 | `main`（2048B）/ `led`（512B，500ms 翻转）/ `tshell` / `tidle0` |

验收：`python tools/verify/verify_phase1.py` → **15 passed, 0 failed**
（含经 SWD 读回 P102 的 `PmnPFS` 引脚电平：`led on`→PODR=1、`led off`→PODR=0、blink→1.4s 内 3 次翻转）。

```text
RA8D1 Vision Board - Phase 1
RT-Thread Nano 5.0.2, CPU 480000000 Hz, tick 1000 Hz
[heartbeat] tick=4016 heap total=65440 used=7280 max=7280
msh >led on
led on
```

### Phase 1 踩到的三个坑

| 坑 | 现象 | 根因 / 修法 |
|---|---|---|
| `IOPORT_CFG_NMOS_ENABLE` | TXD 完全无输出 | 0x40 = NMOS 开漏，官方 BSP 只用在 P408/P409；P208/P209 必须去掉 |
| `CSR_b.TDRE` 门控 | 每 16 字节丢字符 | SCI_B 跑 FIFO 模式（深度 16），TDRE 只反映 TDR/移位寄存器握手；改用 `FTSR_b.T < fifo_depth`（与驱动自己的 TXI ISR 一致） |
| RT-Thread 打成静态库 | finsh 永不启动，`help` 无任何回显，无警告无报错 | 链接器只在归档成员能解析**未定义符号**时抽取它；`shell.c` 的唯一入口是 `INIT_APP_EXPORT` 产生的 `.rti_fn.6` **数据段**，不是符号 → 整个成员被丢弃。改把 RT-Thread 源码直接编进 executable（`--gc-sections` 仍会裁剪无用代码） |

> 判据：`arm-none-eabi-nm build/firmware.elf | grep __rt_init` 必须出现
> `__rt_init_finsh_system_init`；静态库方案下只有 4 个 marker、没有它。

### 实测数据（Phase 1）

- 双构零警告：Debug FLASH 39128B(1.87%) / RAM 92024B(8.77%)；Release FLASH 34236B(1.63%) / RAM 91936B(8.76%)
- 心跳 tick 间隔 2003（1kHz 准确），heap total 65440 / used 7280 / max 7280

## Phase 2：GLCDC + RGB 面板（800×480，无 LVGL）

GLCDC 层 1（RGB565）直取 SDRAM framebuffer，TCON 驱动 RGB 面板，应用层提供
`lcd init|info|stat|pattern N|fill HEX|bl on|off` 串口命令。帧缓冲 `g_lcd_fb`
放 `.sdram`（0x68000000，NOLOAD，768000B）；`BSP_CFG_DCACHE_ENABLED=0` 使 SDRAM
窗口天然一致，无需 Cache 维护。

| 项 | 值 |
|---|---|
| PCLK | LCDCLK 240MHz / 8 = **30MHz** → 刷新率 30e6/(1024×525) = **55.8Hz** |
| 时序 | 1024×525，back porch 46/23，sync width 1，同步低有效，DE 高有效（与官方 BSP 逐值一致） |
| 输出 | RGB666 大端，TCON hsync=PIN_0 / vsync=PIN_1 / de=PIN_3 |
| 引脚 | 全部集中在 `bsp_pin.c` 一张表一次 IOPORT Open；P1011 背光、P1104 面板复位 |
| 图案 | 5 种：黑 / 白 / 8 色条 / RGB 渐变 / 棋盘格 |

验收：`python tools/verify/verify_phase2.py` → **14 passed, 0 failed**
（含 SWD 读回 framebuffer 色条样本、`fill` 全屏落 SDRAM、P1011 背光电平、
`GR[0]` RENB/BASE/欠载锁存）。

> ⚠️ `STMON.L2UNDF=1` 为 FSP 驱动固有良性伪影：驱动无条件武装 GR[1] line-detect
> （`CLUTINT_b.LINE`），而 layer 2 透明且 `RENB=0` 从不取数，粘滞位清除后 1-2 帧
> 内复现。硬判据取 `L1UNDF==0`（层 1 从未欠载）。完整取证链见
> `documents/phase2-report.md` 第 5 节。

### 实测数据（Phase 2）

- 双构零警告：Debug FLASH 47328B(2.26%) / 内部 RAM 92128B(8.79%)；Release FLASH 41016B(1.96%) / RAM 91992B(8.77%)
- 另：`.sdram` NOLOAD 768000B（framebuffer，32MB 的 2.29%，不占 Flash/内部 RAM）

## Phase 3：Camera（OV5640 + CEU 采集）

OV5640（QVGA 320×240 RGB565，8-bit DVP）→ CEU → SDRAM framebuffer 全链贯通。
SCCB 用 P1013/P1014 位bang（100kHz）；XCLK 用 GPT7 PWM @ **P1006**（GTIOC7B，
24MHz = PCLKA 120MHz/5）；CEU 配置逐值复刻官方 `g_ceu_qvga`，FRAME_END 中断
（slot 1 = EVENT_CEU_CEUI）+ 信号量完成抓帧同步。串口命令
`cam init / scan / snap / stat / rd A R / bar on|off`。

> ⚠️ **XCLK 引脚根因**：P1011 是 GTIOC6B（官方 g_timer6 背光 PWM 输出），
> GTIOC7B 在 **P1006**。官方工程 GPT 双实例（ch6/ch7）+ 双 GPT1 引脚导致
> "寄存器逐位一致却无输出"的假象，最终以 P1006 PSEL 扫描实测 PWM 定案。
> 完整 9 步取证链见 `documents/phase3-report.md` 第 4 节。

验收：`python tools/verify/verify_phase3.py` → **14 passed, 0 failed**
（含 SWD 读回 framebuffer、colorbar 8 条带 ~40px 周期/行均匀/确定性分析、
传感器 ID 寄存器读回 0x56/0x40）。

### 实测数据（Phase 3）

- 双构零警告：Debug FLASH 60448B(2.88%) / 内部 RAM 67616B(6.45%)；Release FLASH 50944B(2.43%) / RAM 67472B(6.43%)
- 另：SDRAM 合计 921600B（LCD fb 768000 + camera fb 153600，32MB 的 2.75%）

## 参考

官方 BSP（只读参考，不参与构建）：`E:/cnb/git/sdk-bsp-ra8d1-vision-board-master`
（https://github.com/RT-Thread-Studio/sdk-bsp-ra8d1-vision-board）
