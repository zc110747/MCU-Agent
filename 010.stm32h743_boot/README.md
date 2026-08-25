# STM32H743ZIT6 QSPI Flash 功能测试 Demo

## 1. 项目概述

基于 **LXB743ZI-P1 开发板**（STM32H743ZIT6）的 QSPI Flash 功能测试 Demo。
目标：验证板载 QSPI Flash 在 **两种访问模式** 下均工作正常：

| 模式 | 说明 | 验证方法 |
|------|------|----------|
| **直接 HAL 读取模式（Indirect）** | 通过 `HAL_QSPI_Command` / `HAL_QSPI_Receive` 发送读命令取数据 | 写 256B 图案 → 间接读回 → 比对 |
| **内存映射模式（Memory-Mapped / XIP）** | 进入映射模式后，Flash 直接挂在 `0x90000000`，按指针读 | 经 `0x90000000` 指针读取 → 与写入图案比对 |

测试结论：**两种模式均 PASS**（详见第 7 节）。

> **USB MSC（U 盘）已集成但未启用**：当前 flash 为空且无主机连接，U 盘测试暂不进行。
> TinyUSB + MSC 后端代码（`bsp/usb_board.c`、`bsp/msc_qspi.c`、`bsp/usb_desc.c`、`bsp/tusb_config.h`）已就绪，
> 后续只需在 `main.c` 调用 `BSP_USB_Init()` + `tusb_init()` + `tud_task()` 即可启用（详见第 10 节）。

---

## 2. 硬件资源（解析自原理图 LXB743ZI-P1原理图.pdf）

| 资源 | 配置 |
|------|------|
| MCU | STM32H743ZIT6（Cortex-M7，480 MHz） |
| QSPI Flash | 板上实测 JEDEC ID `68 40 17` → **Boya BY25Q64**（Winbond W25Q64 命令兼容克隆，8 MB / 64 Mbit） |
| QSPI CLK | PF10 (AF9) |
| QSPI NCS | PG6 (AF10) |
| QSPI IO0 | PF8 (AF10) |
| QSPI IO1 | PF9 (AF10) |
| QSPI IO2 | PF7 (AF10) |
| QSPI IO3 | PF6 (AF10) |
| 调试串口 | USART1，PA9(TX)/PA10(RX) → ST-Link VCP（本机 COM19），115200 8N1 |
| 运行状态 LED | PG7，输出，低电平点亮，推挽，上拉（500 ms 心跳闪烁 = 固件运行中） |
| HSE | 外部无源晶振 25 MHz |
| 调试/烧录 | ST-Link V2（SWD）|

> ⚠️ 原理图标注为 W25Q64JV（MFR `0xEF`），但本板实物为 Boya BY25Q64（MFR `0x68`）。
> 二者命令集完全兼容（JEDEC ID / 读 / 页编程 / 扇区擦除 / 四线 QE 使能一致），测试全部通过。

---

## 3. 工程结构（模块化）

```
stm32_qspi/
├── CMakeLists.txt            # cmake + ninja 构建脚本（arm-none-eabi-gcc）
├── stm32h743xix_flash.ld    # 链接脚本（FLASH/SRAM，_estack 在 DTCM 顶部）
├── openocd.cfg              # VSCode 仿真调试用 OpenOCD 配置（仅起服务，不烧录）
├── openocd_flash.cfg        # 独立烧录配置（flash + verify + reset + exit）
├── capture.py               # 通过 SWD 读取 RAM 日志缓冲，抓取测试输出
├── .vscode/                 # VSCode 仿真环境配置（见第 4 节）
│   ├── launch.json          # Cortex-Debug 启动配置（ST-Link + OpenOCD）
│   ├── tasks.json           # 构建/清理/烧录任务
│   ├── settings.json        # cmake/ninja/gcc 工作区设置
│   ├── c_cpp_properties.json# IntelliSense 包含路径
│   └── extensions.json      # 推荐扩展
├── app/
│   ├── main.c               # 系统时钟、测试流程编排、结果判定
│   └── stm32h7xx_it.c       # 中断处理（SysTick_Handler → HAL_IncTick）
├── bsp/
│   ├── uart.c / uart.h      # USART1 调试输出 + RAM 日志镜像缓冲
│   ├── led.c / led.h        # 运行状态 LED（PG7，低电平点亮，500 ms 心跳闪烁）
│   ├── qspi.c / qspi.h      # W25Q64 兼容 QSPI 驱动（间接/映射/擦写/QE）
│   ├── qspi_test.c/.h       # QSPI 自测（间接+映射），已封装、可选，当前不编译进固件
│   ├── usb_board.c / .h     # TinyUSB 板级初始化（PA11/PA12 FS + CRS + NVIC）
│   ├── msc_qspi.c           # TinyUSB MSC 回调：以 HAL 间接模式把 QSPI 作为 U 盘
│   ├── usb_desc.c           # USB 描述符（MSC-only，供应商/产品字符串）
│   ├── tusb_config.h        # TinyUSB 配置（CFG_TUD_MSC=1，MSC-only）
│   └── syscalls.c           # 最小 newlib 系统调用桩（去 nosys 链接告警）
├── third_party/
│   ├── tinyusb/             # TinyUSB 0.21.0（DWC2 端口，STM32H7 FS）
│   └── FatFs/               # FatFs（已从参考工程导入，MSC 设备侧暂未使用）
├── Drivers/
│   ├── CMSIS/Include        # Cortex-M7 内核头 + cmsis_compiler 等
│   ├── CMSIS/Device/...     # STM32H7 CMSIS 设备头、startup、system 文件
│   └── HAL/                 # STM32H7 HAL 驱动（Inc/Src，含 Legacy）
└── build/                   # 构建产物（.elf/.bin/.hex/.map）
```

---

## 4. 仿真 / 调试环境（VSCode + Cortex-Debug）

依赖（已加入系统 PATH）：`arm-none-eabi-gcc`、`cmake`、`ninja`、`openocd`、`python3`。

推荐扩展（`Ctrl+Shift+X` 安装，或参考 `.vscode/extensions.json`）：

- `marus25.cortex-debug`   — 嵌入式调试核心
- `ms-vscode.cpptools`     — C/C++ IntelliSense
- `ms-vscode.cmake-tools`  — CMake 集成（可选）

### 启动调试（单步仿真）

1. `F5` 或 *Run → Start Debugging*，选择 **"QSPI Debug (ST-Link + OpenOCD)"**。
   - 该配置 `preLaunchTask` 自动执行 **CMake Build**；
   - `runToEntryPoint: main` 复位后停在 `main()`；
   - 加载 `build/stm32_qspi.elf`，可在 `app/main.c`、`bsp/qspi.c` 内打断点单步。
2. 另一配置 **"QSPI Attach (no build)"** 用于不重新编译、直接挂载正在运行的板子。

### 配置要点

- `openocd.cfg`：**只**启动调试服务（SWD + STM32H7 目标），**不**自动烧录，
  由 Cortex-Debug 自行加载 ELF，避免 `${PROJECT_ELF}` 变量未定义问题。
- 如需寄存器视图，可将官方 `STM32H743x.svd` 放到
  `Drivers/CMSIS/Device/ST/STM32H7xx/Include/` 并在 `launch.json` 的 `svdFile` 指向它
  （ST 的 SVD 随 Cube 固件包分发，未包含在 GitHub 源码仓）。

---

## 5. 构建步骤

```bash
# 配置（Ninja + Debug）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 构建（零警告目标）
cmake --build build
# 产物： build/stm32_qspi.elf / .bin / .hex

# Release 构建
cmake --build build --config Release
```

链接脚本与编译选项在 `CMakeLists.txt` 中集中管理：
`-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -DSTM32H743xx -DUSE_HAL_DRIVER`。

---

## 6. 烧录与测试

### 6.1 烧录

```bash
# 方式 A：VSCode 任务 "Flash (openOCD)"（= openocd -f openocd_flash.cfg）
# 方式 B：命令行
openocd -f openocd_flash.cfg
```

### 6.2 抓取测试输出（RAM 日志法）

ST-Link 虚拟串口在复位瞬间会丢失启动日志，因此固件将每一行输出**镜像到 RAM 缓冲
`g_uart_log`**，再用 OpenOCD 通过 SWD 直接读取该缓冲，稳定可靠：

```bash
python capture.py --wait 7
```

脚本自动从 ELF 解析 `g_uart_log` 符号地址，复位运行 → 等待 → halt → dump → 解码打印。

> 串口（COM19，115200）也直接输出同样内容；若需实时看心跳点，可用任意串口助手连接 ST-Link VCP。

---

## 7. 测试结果

```
=================================================
 STM32H743ZIT6 QSPI Flash Test (W25Q64 compatible)
=================================================
 CPU Clock : 480 MHz

[1] Init QUADSPI peripheral... OK
[2] Read JEDEC ID (cmd 0x9F)... OK  -> MFR=0x68 MemType=0x40 Cap=0x17
    Detected: Boya BY25Q64 (W25Q64-compatible clone, 8MB)

[TEST A] Indirect (HAL) read/write mode
  Erase sector @0x00000000... OK
  Program 256 bytes... OK
  Read back (HAL indirect)... OK
    Written pattern: 11 18 1F 26 ... E3 EA
    Read  (indirect): 11 18 1F 26 ... E3 EA
  RESULT: PASS (indirect read matches written data)

[TEST B] Memory-mapped (XIP) mode (reads @0x90000000)
  Enter memory-mapped mode (cmd 0xEB, quad)... OK
  Read flash via 0x90000000 pointer... OK
    Read  (memory-mapped): 11 18 1F 26 ... E3 EA
  RESULT: PASS (memory-mapped read matches written data)
  Exit memory-mapped mode... OK

=================================================
 OVERALL: PASS - QSPI flash works in BOTH modes
=================================================
```

固件体积：`text 55,720 B / data 2,212 B / bss 6,680 B`（FLASH 约 56 KB，RAM 约 9 KB）。

---

## 8. 调试过程中解决的问题

| # | 现象 | 根因 | 解决 |
|---|------|------|------|
| 1 | 上电即 HardFault（CFSR 用法错误） | HCLK=480 MHz 仅设 `FLASH_LATENCY_4`，取指错位 | 改为 `FLASH_LATENCY_6`（H7 @480MHz 需 6 等待） |
| 2 | 串口只输出 `\r\n` 后卡死 | `SysTick_Handler` 未实现，HAL 时基中断落到 `Default_Handler` 死循环 | 新增 `app/stm32h7xx_it.c` 实现 `SysTick_Handler → HAL_IncTick()` |
| 3 | 内存映射模式读取为乱码 | W25Q64 四线读（0xEB）需先置 **QE** 位（状态寄存器 2）；且 PF8/PF9 复用应为 **AF10**（原理图注释正确但代码误用 AF9） | 修正 IO0/IO1 为 AF10；`BSP_QSPI_Init` 中增加 `QSPI_EnableQuadMode()` 置 QE |
| 4 | 启动日志抓不全 | ST-Link VCP 在复位瞬间缓冲溢出丢数据；`subprocess` 阻塞复位期间串口读取被饿死 | 固件镜像日志到 RAM `g_uart_log`，用 OpenOCD 经 SWD 直接 dump |
| 5 | 链接告警 `_close/_write ... not implemented` | 默认 `-specs=nosys.specs` 链接了工具链的空桩 | 自写 `bsp/syscalls.c` 提供最小桩并移除 `nosys.specs`；另加 `--no-warn-rwx-segments` 消 RWX 段告警 |
| 6 | 二次 dump 日志为空 | 增删源文件改变 `.bss` 布局，`g_uart_log` 地址漂移，硬编码 dump 地址失效 | `capture.py` 改为从 ELF 动态解析符号地址 |

最终构建 **零警告**，烧录 **Verified OK**，硬件实测 **两种 QSPI 模式均 PASS**。

---

## 9. TinyUSB MSC（U 盘）集成状态

已按用户指令将 TinyUSB + MSC 移植完毕，目标是在 PC 上把板载 8 MB QSPI 当作 U 盘（虚拟 U 盘）读写。

**当前状态：已启用并编译通过。**
- 启动流程：`BSP_QSPI_Init()`（HAL 间接模式，不进入 XIP）→ `FS_PrepareForMassStorage()`（空盘自动 FAT 格式化并卸载，已存在则直接卸载）→ `BSP_USB_Init()` + `tusb_init()` → 主循环 `tud_task()`。
- 已移除启动时的 QSPI 自测（含 XIP memory-mapped 会话），因为 XIP 会污染 QUADSPI 外设状态，导致后续间接访问（FatFs / USB MSC 写路径）挂死。仅保留 HAL 间接模式的初始化与读写。

### 已落地的代码

| 文件 | 作用 |
|------|------|
| `bsp/tusb_config.h` | TinyUSB 配置：`CFG_TUD_MSC=1`，`CFG_TUD_CDC=0`（纯 U 盘）；`CFG_TUD_MSC_EP_BUFSIZE=512` |
| `bsp/usb_board.c` | `BSP_USB_Init()`：开 HSI48 + CRS（SOF 校准 48MHz）、配置 PA11/PA12 AF10、开 USB 电压检测、使能 OTG_FS 时钟与中断（`OTG_FS_IRQn`） |
| `bsp/usb_desc.c` | 设备/配置/字符串描述符（厂商 `STM32H7`、产品 `QSPI_DISK`） |
| `bsp/msc_qspi.c` | MSC 回调：`tud_msc_read10_cb`（HAL 间接读）、`tud_msc_write10_cb`（读-改-擦-写，4KB 扇区粒度，因 Flash 不可原地写）、`tud_msc_capacity_cb`（8MB / 512B 块） |
| `app/main.c` | 启动即启用 USB MSC；`stm32h7xx_it.c` 中 `OTG_FS_IRQHandler` 转发 `tud_int_handler` |

注意：`stm32h7xx_it.c` 已包含 `OTG_FS_IRQHandler`（转发 `tud_int_handler`），无需再改。

---

## 10. 关键源码索引

- 系统时钟：`app/main.c :: SystemClock_Config()` — HSE 25MHz → PLL1 → sys_ck 480MHz，HCLK 240MHz，PCLK 120MHz，FLASH_LATENCY_6。
- QSPI 初始化/读写/映射：`bsp/qspi.c :: BSP_QSPI_Init / EraseSector / WritePage / ReadIndirect / EnableMemoryMapped`。
- QSPI 自测（可选，未编译进固件）：`app/qspi_test.c :: BSP_QSPI_RunSelfTest()`（TEST A 间接、TEST B 映射，失败计数汇总 OVERALL），封装后可按需单独调用，不参与启动流程。
- 调试输出：`bsp/uart.c :: BSP_UART_Printf()`（HAL_UART_Transmit + 镜像 `g_uart_log`）。
```
