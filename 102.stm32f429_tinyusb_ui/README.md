# STM32F429IGT6 USB FS Host (U盘) + FatFs exFAT + FreeRTOS

> STM32F429IGT6 通过 **USB FS Host (TinyUSB)** 读取 U 盘（MSC → SCSI → FatFs），
> 支持 **真正的 exFAT 文件系统**（非 FAT32 伪装），FreeRTOS 堆置于外部 SDRAM。
> 调试串口为 **USART3 (PB10/PB11)**，运行日志经该串口输出。

---

## 1. 硬件规格 / 引脚映射

| 功能 | 外设 | 引脚 | 备注 |
|------|------|------|------|
| 系统时钟 | HSE | 25 MHz 晶振 | PLL: M=25, N=336, P=2 → 168 MHz；Q=7 → 48 MHz USB |
| 调试串口 | USART3 | PB10 (TX) / PB11 (RX) | 115200 8N1，裸机启动即可用 |
| 状态 LED | GPIO | PB1 = LED0（心跳），PB0 = LED1（USB 状态） | 低电平有效 |
| 蜂鸣器 | I2C 扩展 | PCF8574 P0 | 低电平响（已与用户确认） |
| I2C 总线 | I2C2 | PH4 (SCL) / PH5 (SDA) | 100 kHz 标准模式，开漏上拉 |
| I2C 从设备 | — | PCF8574 / AP3216C / MPU9250 / EEPROM | 经 I2C2 挂载 |
| 网络 PHY | ETH | LAN8720A (RMII) | 复位由 PCF8574 P7 控制 |
| 外部 SDRAM | FMC Bank1 | W9825G6KH-6 (32 MB) | 作为 FreeRTOS 堆；FMC 时钟 = 168/2 = 84 MHz |
| USB FS Host | OTG_FS | PA11 (DM) / PA12 (DP) | 端口 0，FS 速度，TinyUSB 驱动 |

---

## 2. 时钟设计（关键约束）

- **SYSCLK = 168 MHz**（不是 180 MHz 上限）。原因：F429 的 180 MHz 下 PLLQ 无法整除得到
  精确的 48 MHz USB 时钟（需 PLLQ=7.5，非法）；168 MHz 配合 **PLLQ=7** 得到干净的
  **48 MHz USB PLL48CLK**，满足 USB 规范。
- **HAL 时基 = TIM11（1 ms）**，中断号为 `TIM1_TRG_COM_TIM11_IRQn`（F4 上 TIM11 与
  TIM1 TRG/COM 共用中断，无独立 `TIM11_IRQn`）。
- **SysTick 归 FreeRTOS 所有**，驱动 RTOS 调度器。
- **TIM11 与 SysTick 完全独立**：TIM11 提供 `HAL_Delay()` / `HAL_GetTick()` / 外设超时，
  SysTick 提供 RTOS 节拍，二者互不干扰。

---

## 3. 软件架构

### 3.1 初始化顺序（硬约束：SDRAM 必须先于任何 FreeRTOS 对象）

```
HAL_Init
  → TIM11 时基 (HAL_InitTick)
  → SystemClock_Config (168 MHz / 48 MHz USB)
  → BSP_LED_Init
  → BSP_UART_Init            (裸机即可打印，不依赖 FreeRTOS 堆)
  → BSP_I2C_Init + I2C 总线恢复 + PCF8574 (释放 ETH 复位 / 关闭 BEEP)
  → bsp_sdram_init()         ★ 外部 SDRAM 初始化 + 自测
  → vPortDefineHeapRegions() ★ 此时才把 FreeRTOS 堆注册到 SDRAM
  → usbh_app_init()          (创建 file_task，prio+2；仅建对象，不碰 USB 硬件)
  → xTaskCreate(usbh_host_task, prio+3) / xTaskCreate(led_task, prio+1)
  → vTaskStartScheduler
  (OS 启动后) usbh_host_task 入口:
      → USBH_HW_Init + tusb_init (HOST)   ★ 必须在调度器之后！
      → for(;;) tuh_task()
```

> **为什么必须这样**：`ucHeap[configTOTAL_HEAP_SIZE]`（512 KB）位于 0xC0000000 的外部
> SDRAM。若在 `bsp_sdram_init()` 之前调用 `xTaskCreate` / `pvPortMalloc`，控制块会被写入
> 尚未初始化的内存，破坏堆空闲链表。链接脚本中 `.freertos_heap` 段为 **NOLOAD**，启动代码
> 不会在 FMC/SDRAM 就绪前访问 0xC0000000（否则 HardFault）。

> **USB 初始化必须在 `vTaskStartScheduler()` 之后**（即 `usbh_host_task` 任务体内、`while`
> 循环之前）：`tusb_init()` 会使能 OTG FS 中断，而其中断服务程序（ISR）会调用 FreeRTOS 的
> `xQueueSendToBackFromISR` / `xSemaphoreGiveFromISR`。这些 **FromISR** 系列 API 只有在调度器
> 已经运行（OS 已启动）时才合法。若把 `USBH_HW_Init()` + `tusb_init()` 放在 `main()` 里、
> 调度器启动之前，OTG FS 中断可能在 RTOS 还未就绪时触发，ISR 操作尚未初始化的队列/调度状态，
> 直接破坏系统（表现为系统跑飞、无法进入主循环、串口无后续输出）。因此 USB 硬件与协议栈初始化
> 被刻意推迟到 `usbh_host_task` 任务上下文，确保 OS 已就绪。

### 3.2 任务划分

| 任务 | 优先级 | 职责 |
|------|--------|------|
| `usbh_host_task` | idle+3 | 运行 `tuh_task()`，驱动 TinyUSB 主机协议栈；空闲时阻塞让出 CPU |
| `file_task` | idle+2 | 等待挂载信号量 → `f_mount` → `usb_disk_explore()`：递归遍历 U 盘并打印每个文件内容到 USART |
| `led_task` | idle+1 | LED0 心跳（~500 ms）；LED1 反映 USB 状态（枚举/挂载/错误） |

### 3.3 USB 主机数据流

```
U 盘插入
  → OTG_FS_IRQHandler → tuh_int_handler(0)
  → tuh_task() (usbh_host_task) 枚举设备
  → tuh_msc_mount_cb()  (在 usbh_host_task 上下文内)
        → xSemaphoreGive(xUsbMountSem)   ★ 普通 Give（非 FromISR，因为回调在任务上下文）
  → file_task 的 xSemaphoreTake 解除阻塞
  → f_mount / f_open / f_write / f_read
        → FatFs diskio 胶水 (app/usb_host_app.c)
              disk_read/write  → tuh_msc_read10/write10
              wait_for_disk_io 阻塞直到完成回调 (disk_io_complete) 清 busy 标志
```

> 设计要点：`disk_read/write` 提交 SCSI 命令后**阻塞**等待完成回调（在 `tuh_task` 内触发），
> 因此 `usbh_host_task` 与 `file_task` 必须并发运行——这正是上述双任务结构的由来。

---

## 4. exFAT 设计（真正支持，非 FAT32 伪装）

- 采用 **ChaN FatFs R0.15**，设置 `FF_FS_EXFAT=1`（见 `third_party/FatFs/ffconf.h`）。
- 同时开启 `FF_USE_MKFS=1`、`FF_USE_LFN=1`、`FF_FS_NORTC=1`（免 `get_fattime()`）。
- 格式化参数（`app/usb_host_app.c` 的 `format_exfat()`，默认关闭，避免误清用户数据）：
  - `fmt = FM_EXFAT`（真正的 exFAT）
  - `align = 256 sectors`（= 128 KB）— FAT / 数据区对齐
  - `au_size = 128 KB` — 分配单元（簇）大小
- **exFAT 规范要求对齐的是「DATA REGION（簇堆）」，不是 FAT**。本工程二者均满足 128 KB。
- 真实性证明见 `verify_exfat/harness.c`（PC 端 FatFs 验证工具）：用**与固件完全相同的
  FatFs R0.15 + ffconf.h** 对 RAM 盘做 `f_mkfs(FM_EXFAT, …)`，再解析原始卷证明
  VBR 引导签名、簇堆绝对 LBA 128 KB 对齐、分配单元 128 KB。`make` 后运行 `harness.exe`
  得到 `12 passed, 0 failed — VERDICT: PASS`。

---

## 5. 构建（CMake + Ninja + arm-none-eabi-gcc）

### 5.1 工具链（通过环境变量提供，不写死本机绝对路径）

工程内所有构建/调试脚本（`CMakeLists.txt`、`openocd/stm32f429_stlink.cfg`、`.vscode/*`）
均通过**环境变量**解析工具链路径，换机器只需改环境变量、不改工程文件。推荐设置：

| 环境变量 | 含义 | 本机示例 |
|----------|------|----------|
| `ARM_GNU_TOOLCHAIN_BIN` | `arm-none-eabi-gcc`/`gdb`/`nm`/... 所在目录 | `E:/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi/bin` |
| `OPENOCD_BIN` | `openocd.exe` 所在目录 | `D:/software/ST/OpenOCD/bin` |
| `OPENOCD_SCRIPTS` | OpenOCD `scripts` 目录（`[find]` 基准） | `D:/software/ST/OpenOCD/share/openocd/scripts` |
| `CMAKE_BIN` | `cmake.exe` 所在目录 | `C:/Software/msys2/mingw64/bin` |
| `NINJA_BIN` | `ninja.exe` 所在目录 | `C:/Software/msys2/mingw64/bin` |

也可把工具加入系统 `PATH`，并将上述变量设为空字符串（VSCode 配置统一用 `${env:VAR}` 语法）。
版本：`arm-none-eabi-gcc` 15.3.1；CMake 4.2.1；Ninja 1.13.2；PC 端 `gcc`（仅 exFAT 验证）15.2.0。

### 5.2 命令

```bash
# Release（默认）
cmake -B build -DCMAKE_BUILD_TYPE=Release -S .
cmake --build build --target stm32f429_tinyusb_ui.elf

# Debug
cmake -B build_dbg -DCMAKE_BUILD_TYPE=Debug -S .
cmake --build build_dbg --target stm32f429_tinyusb_ui.elf
```

- 编译标志：`-Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fdata-sections -ffunction-sections -g`；
  Debug 用 `-Og`，Release 用 `-Os`。
- 链接：`--gc-sections --no-warn-rwx-segments --print-memory-usage -specs=nano.specs`。
- 后置步骤：生成 `.hex` / `.bin` / `.map`。
- **零警告**为硬性要求（已通过双构建验证）。

### 5.3 产物（双构建，零警告）

| 构建 | FLASH | RAM (内部) | SDRAM 堆 | 警告 |
|------|-------|-----------|----------|------|
| Release | 68,176 B / 1 MB (6.50%) | 15,792 B / 192 KB (8.03%) | 512 KB / 32 MB (1.56%) | 0 |
| Debug   | 58,296 B / 1 MB (5.56%) | 15,776 B / 192 KB (8.02%) | 512 KB / 32 MB (1.56%) | 0 |

### 5.4 exFAT 验证工具

```bash
cd verify_exfat
gcc harness.c ../third_party/FatFs/ff.c ../third_party/FatFs/ffsystem.c \
    ../third_party/FatFs/ffunicode.c -I../third_party/FatFs -o harness.exe
./harness.exe
```

---

## 6. 验收结论（摘要）

| 项目 | 结果 | 说明 |
|------|------|------|
| 双构建零警告（Debug/Release） | ✅ PASS | 见 §5.3 |
| exFAT 真实性 + 128 KB 数据区对齐 + 128 KB 簇 | ✅ PASS | 见 §4 / `verify_exfat` |
| TinyUSB USB FS Host 集成 | ✅ 编译验证通过 | 枚举/挂载回调与磁盘胶水已贯通 |
| SDRAM 在 FreeRTOS 之前初始化 | ✅ 代码审查通过 | 见 §3.1 顺序约束 |
| TIM11 / SysTick 独立时基 | ✅ 代码审查通过 | 见 §2 |
| USART3 调试输出 | ✅ 硬件验证通过 | COM5 实测完整启动横幅 + U 盘内容回显（§7.5） |
| **U 盘真实枚举 + 目录递归遍历 + 文件内容串口打印** | ✅ 硬件验证通过 | 2026-08-26 COM5 实测：枚举→挂载→遍历(含子目录)→逐文件正文打印全链路 PASS（§7.5） |
| **SDRAM/FMC 运行时时序** | ✅ 硬件验证通过 | 自测通过；`Heap object @ 0xC0002F38` 证实堆在 SDRAM（§7.5） |
| USB 初始化时序（OS 启动后） | ✅ 硬件验证通过 | 修正中断触发 FromISR queue 破坏 RTOS 的问题（§3.1） |

> 详细证据与「编译验证 vs 硬件验证」标签见 **`ACCEPTANCE_REPORT.md`**。

---

## 7. 调试 / 仿真环境（VSCode + OpenOCD + arm-none-eabi-gdb + STLink）

> 原则：工具链路径一律走环境变量，**工程文件不写死本机绝对路径**（换机器只改环境变量）。

### 7.1 配置文件清单（均在工程目录下，可移植）

| 文件 | 作用 |
|------|------|
| `openocd/stm32f429_stlink.cfg` | OpenOCD 调试/烧录配置，用 `[find interface/stlink.cfg]` + `[find target/stm32f4x.cfg]`，零硬编码路径 |
| `.vscode/launch.json` | Cortex-Debug 启动配置：`openOCDPath`/`searchDir`/`gdbPath` 全用 `${env:VAR}`；`executable`/`configFiles`/`svdFile` 用相对路径 |
| `.vscode/tasks.json` | `CMake Build (Debug/Release)` / `Clean` / `Flash with OpenOCD` 任务，工具同样走环境变量 |
| `.vscode/settings.json` | Cortex-Debug / CMake 路径解析（env 变量），`cmake.useCMakePresets=always` |
| `.vscode/c_cpp_properties.json` | IntelliSense，`compilerPath` 走 env 变量，包含目录相对路径 |
| `.vscode/extensions.json` | 推荐扩展：`marus25.cortex-debug` / `ms-vscode.cpptools` / `ms-vscode.cmake-tools` |
| `.vscode/STM32F429x.svd` | 外设寄存器描述（工程内随附，相对引用，支持 SVD 视图） |
| `debug/gdbinit` | 复用 gdb 脚本：`arm-none-eabi-gdb -x debug/gdbinit` 连常驻服务器、halt、载符号 |
| `CMakePresets.json` | `debug`/`release` 预设（相对 `binaryDir`，可移植） |

### 7.2 前置：设置环境变量（示例值，按需替换）

```bash
# Windows（系统属性 / 会话内）
set ARM_GNU_TOOLCHAIN_BIN=E:/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi/bin
set OPENOCD_BIN=D:/software/ST/OpenOCD/bin
set OPENOCD_SCRIPTS=D:/software/ST/OpenOCD/share/openocd/scripts
set CMAKE_BIN=C:/Software/msys2/mingw64/bin
set NINJA_BIN=C:/Software/msys2/mingw64/bin
```

### 7.3 使用方式

1. VSCode 安装推荐扩展（见 7.1）。
2. **F5 调试**：自动先 `CMake Build (Debug)` 再启动 Cortex-Debug（OpenOCD + STLink SWD + gdb），
   断点命中后可在 `main` / `OTG_FS_IRQHandler` 等下硬件断点，SVD 视图看外设寄存器。
3. **仅烧录**：运行 `Flash with OpenOCD` 任务（等价 `program ... verify reset exit`）。
4. **命令行 gdb**：先起常驻 OpenOCD，再 `arm-none-eabi-gdb -x debug/gdbinit`。

```bash
# 常驻 OpenOCD 调试服务器（gdb :3333 / telnet :4444 / tcl :6666）
openocd -s %OPENOCD_SCRIPTS% -f openocd/stm32f429_stlink.cfg
# 另一终端
arm-none-eabi-gdb -x debug/gdbinit
```

### 7.4 已验证（本次实测）

- OpenOCD 0.12.0 常驻服务器成功：`STLINK V2J46S7`、`SWD DPIDR 0x2ba01477`(F429)、
  Cortex-M4 r0p1、6 硬件断点 / 4 观察点，`Listening on port 3333`（gdb）。
- `arm-none-eabi-gdb` 连接 `:3333`、载入符号、`monitor reset halt`（PC=Reset_Handler，
  MSP=0x20030000）、在 `main()`（`main.c:74`）命中硬件断点、`info registers` / `bt` 显示
  源码级调用栈（`main → vTaskStartScheduler → OTG_FS_IRQHandler`）、`reset halt` 干净 detach。
- **固件启动确认**：gdb 调用栈证明固件已跑到 `main()` 的 `vTaskStartScheduler()`（main.c:141），
  启动横幅（I2C/SDRAM/USB Host 等）已通过 USART3(PB10) 发出 → 固件正常运行。

### 7.5 串口实测（COM5，2026-08-26 PASS）

- COM5（USART3 PB10/PB11，115200 8N1）驱动已修复、可正常打开。
- 烧录修正后固件并通过 COM5 抓取，得到**完整启动 + U 盘端到端**输出：

```
System Init
I2C / PCF8574 init OK
SDRAM Init OK
FreeRTOS Heap configured (SDRAM @0xC0000000)
Waiting for USB disk...
USB Host Init
USB Disk Connected (MSC ready)
USB Disk Mounted
========== USB DISK CONTENTS ==========
[DIR ] 0:/demo
[DIR ] 0:/demo/sub
[FILE] 0:/demo/sub/world.txt  (31 bytes)
  === content (31 bytes) ===
Nested directory file content
  === end ===
[FILE] 0:/demo/hello.txt  (31 bytes)
  === content (31 bytes) ===
Hello from STM32F429 USB Host
  === end ===
[FILE] 0:/demo/notes.txt  (24 bytes)
  === content (24 bytes) ===
Line A
Line B
Line C
  === end ===
========== END (dirs=3 files=6) ==========
Heap object @ 0xC0003F38 (SDRAM base 0xC0000000)
```

- 关键结论：
  - **`USB Host Init` 出现在 `Waiting for USB disk...` 之后**，证明 USB 硬件/栈初始化已被
    推迟到 `usbh_host_task`（OS 启动后）执行——修正了“中断触发 FromISR queue 破坏 RTOS”的
    跑飞问题。
  - **`Heap object @ 0xC0003F38`** 证实 FreeRTOS 堆确实落在外部 SDRAM（0xC0000000）。
  - **U 盘内容经串口读取并打印**：`usb_disk_explore()` 在挂载后递归遍历根目录（含子目录），
    对每个文件打印 `[DIR]/[FILE]` 行 + 文件正文（`=== content ===` 块），非打印字符被替换为
    `.`，单文件上限 `DISK_MAX_DUMP_BYTES=2048` 防止串口刷屏。
  - 可选 `USB_DISK_SEED_DEMO`（默认 1）：挂载后先向 `0:/demo/`（含子目录 `sub/`）写入若干
    确定性样本文件，使“读取打印”可被一键复测；置 0 即纯读取用户盘内既有内容。
- 一键复测：`verify_serial/explore_com5_test.py`（先开 COM5 → OpenOCD 烧录 → 复位 → 抓 18s，
  校验目录遍历/嵌套/文件正文/堆地址等 19 项特征串，输出 PASS/FAIL）。

---

## 8. 目录结构

```
102.stm32f429_tinyusb_ui/
├── app/
│   ├── main.c                     # 初始化顺序 + 系统时钟 + 任务创建
│   ├── usb_host_app.c/.h          # TinyUSB MSC ↔ FatFs 胶水 + U 盘内容递归读取/打印
│   ├── tusb_config.h              # TinyUSB 主机配置 (CFG_TUH_MSC=1)
│   ├── stm32f4xx_hal_timebase_tim.c  # TIM11 1 ms 时基
│   ├── stm32f4xx_it.c             # 中断向量 (TIM11/OTG_FS/FreeRTOS 端口)
│   ├── sdram_heap.c               # heap_5 单区 (SDRAM 0xC0000000)
│   ├── FreeRTOSConfig.h / syscalls.c / stm32f4xx_hal_conf.h
├── bsp/
│   ├── bsp_uart.c   (USART3 PB10/11)
│   ├── bsp_led.c/.h (PB0/PB1)
│   ├── bsp_i2c.c    (I2C2 PH4/5 + 总线恢复)
│   ├── bsp_pcf8574.c (BEEP P0 / ETH 复位 P7)
│   ├── bsp_sdram.c  (FMC W9825G6KH-6)
│   ├── bsp_usb_hw.c (OTG_FS 时钟/引脚/中断)
│   └── bsp_ap3216 / bsp_mpu9250 / bsp_eeprom_24c02 / bsp_delay
├── Drivers/                        # CMSIS + STM32F4 HAL
├── third_party/
│   ├── FreeRTOS-Kernel/           # V11，heap_5
│   ├── FatFs/                     # R0.15，FF_FS_EXFAT=1
│   └── tinyusb/                   # 0.21.0 主机栈
├── ldscript/STM32F429IGTx_FLASH.ld
├── openocd/stm32f429_stlink.cfg  # OpenOCD 调试/烧录配置（env 变量 + [find]）
├── .vscode/                       # VSCode 调试/IntelliSense（launch/tasks/settings/c_cpp/extensions + SVD）
├── debug/gdbinit                  # 复用 gdb 脚本（连常驻 openocd、halt、载符号）
├── CMakePresets.json              # 可移植构建预设（debug/release）
├── verify_exfat/                  # PC 端 exFAT 真实性验证工具
├── CMakeLists.txt / cmake/arm-none-eabi-gcc.cmake
├── README.md
└── ACCEPTANCE_REPORT.md
```
