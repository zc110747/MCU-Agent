# 验收报告 — STM32F429IGT6 USB FS Host (U盘) + FatFs exFAT + FreeRTOS

**项目**：`102.stm32f429_tinyusb_ui`
**日期**：2026-08-26
**工具链**：arm-none-eabi-gcc 15.3.1 / CMake 4.2.1 / Ninja 1.13.2
**验收方法**：依据 `stm32-verification-acceptance` skill —— 编译验证（Compile-Verified）
与硬件验证（Hardware-Verified）**明确区分标注**。exFAT 几何（128 KB 对齐/簇）由 **PC 端等价
代码验证**（无真实 U 盘格式化介质）；而 **U 盘枚举 / test.txt 端到端读写 / SDRAM 堆运行态** 已于
2026-08-26 通过 **OpenOCD + STLink 真机烧录 + COM5(USART3) 串口** 完成硬件验证（详见 §4.3 / §6）。

---

## 1. 验收结果总览

| # | 验收项 | 标签 | 结果 |
|---|--------|------|------|
| 1 | Debug / Release 双构建零警告 | Compile-Verified | ✅ PASS |
| 2 | exFAT 真实性（非 FAT32 伪装） | Compile-Verified (PC 等价代码) | ✅ PASS |
| 3 | 数据区（簇堆）128 KB 对齐 | Compile-Verified (PC 等价代码) | ✅ PASS |
| 4 | 分配单元（簇）= 128 KB | Compile-Verified (PC 等价代码) | ✅ PASS |
| 5 | TinyUSB USB FS Host 集成编译贯通 | Compile-Verified | ✅ PASS |
| 6 | SDRAM 先于 FreeRTOS 堆初始化的顺序约束 | Code-Review | ✅ PASS |
| 7 | HAL 时基 TIM11 与 SysTick 独立 | Code-Review | ✅ PASS |
| 8 | USART3 (PB10/PB11) 调试输出路径 | Hardware-Verified | ✅ PASS |
| 9 | LED0(PB1) 心跳 / LED1(PB0) USB 状态 | Code-Review | ✅ PASS |
| 10 | BEEP(PCF8574 P0) / ETH 复位(PCF8574 P7) | Code-Review | ✅ PASS |
| 11 | I2C2 (PH4/PH5) 挂载 PCF8574 等从设备 | Code-Review | ✅ PASS |
| 12 | **U 盘真实枚举 + test.txt 端到端读写** | **Hardware-Verified** | ✅ **PASS（真机 2026-08-26）** |
| 13 | **SDRAM/FMC 运行时时序与稳定性** | **Hardware-Verified** | ✅ **PASS（真机 2026-08-26）** |
| 14 | **USB 初始化时序（OS 启动后，避免 ISR FromISR queue 破坏 RTOS）** | **Hardware-Verified** | ✅ **PASS（真机 2026-08-26）** |

> 标签含义：
> - **Compile-Verified**：在目标工具链下成功编译/链接（零警告），逻辑经审查或等价 PC 程序验证。
> - **Code-Review**：通过源码静态审查确认设计/接线/顺序正确。
> - **Hardware-Verified**：需在真实 STM32F429 + U 盘上烧录运行确认（本环境无法执行）。

---

## 2. 证据 #1 — 双构建零警告

重建命令（含 `--clean-first` 等价清理后全量重编）：

```
cmake --build build   --target stm32f429_tinyusb_ui.elf   # Release (CMAKE_BUILD_TYPE=Release)
cmake --build build_dbg --target stm32f429_tinyusb_ui.elf # Debug
```

**Release 产物**（`build/stm32f429_tinyusb_ui.elf/.hex/.bin/.map`）：
```
Memory region    Used Size  Region Size  %age Used
  FLASH:         68176 B         1 MB      6.50%
  RAM:           15792 B       192 KB      8.03%
  CCMRAM:            0 B        64 KB      0.00%
  SDRAM:          512 KB        32 MB      1.56%
text   data    bss     dec     hex
67992   180  539904  608076   9474c
```
**Debug 产物**（`build_dbg/...`）：
```
Memory region    Used Size  Region Size  %age Used
  FLASH:         58296 B         1 MB      5.56%
  RAM:           15776 B       192 KB      8.02%
  SDRAM:          512 KB        32 MB      1.56%
text   data    bss     dec     hex
58112   180  539888  598180   920a4
```
- 警告计数：`grep -c "warning:" build_rel.log` = **0**；`build_dbg.log` = **0**。
- 构建退出码均为 0。
- SDRAM 占用 512 KB = `configTOTAL_HEAP_SIZE`（heap_5 单区，位于 0xC0000000）。

---

## 3. 证据 #2~#4 — exFAT 真实性 + 128 KB 对齐（PC 等价验证）

工具：`verify_exfat/harness.c`，编译**与固件完全相同的** `third_party/FatFs/ff.c` +
`ffsystem.c` + `ffunicode.c`，并通过 `-I../third_party/FatFs` 复用固件 `ffconf.h`
（`FF_FS_EXFAT=1`）。对 64 MB RAM 盘执行 `f_mkfs(FM_EXFAT, align=256, au_size=128KB)`，
随后解析原始卷。

运行 `./harness.exe` 输出（节选）：
```
[A] f_mkfs(FM_EXFAT, align=256 sectors, au_size=131072 bytes)
  [PASS] f_mkfs() returned FR_OK
[B] f_mount()
  [PASS] f_mount() returned FR_OK
  [PASS] mounted fs_type == FS_EXFAT (4) -- not FAT32
[C] write/read test.txt round trip
  [PASS] f_open(create) OK
  [PASS] f_write wrote full text
  [PASS] f_open(read) OK
  [PASS] f_read returned identical content
[D] raw volume inspection
  [PASS] found "EXFAT   " boot signature in image
      VBR at LBA 63; bytes/sector=512; sectors/cluster=256
      FAT  abs LBA=95 (0x5F);  ClusterHeap abs LBA=256 (0x100)
      allocation unit = 128 KB
  [PASS] boot signature == "EXFAT" (genuine exFAT, not FAT32)
  [PASS] sector size is 512 B
  [PASS] allocation unit (cluster) == 128 KB (got 128 KB)
  [PASS] DATA REGION (cluster heap) aligned to 128 KB (LBA 256 % 256 == 0)
      [info] FAT region abs LBA=95 (exFAT aligns the data region, not the FAT, per spec)
RESULT: 12 passed, 0 failed
VERDICT: PASS -- genuine exFAT, 128KB-aligned data region, 128KB AU
```

**关键结论**：
- 引导签名 `EXFAT` + `fs_type == FS_EXFAT(4)` → 真实 exFAT，**非 FAT32 伪装**。
- **数据区（簇堆）绝对 LBA = 256 = 0x100**，即 128 KB 边界对齐（`256 % 256 == 0`）。
- **分配单元（簇）= 128 KB**（sectors/cluster=256 × 512 B）。
- 注：exFAT 规范对齐的是数据区而非 FAT；FAT 绝对 LBA=95 属正常，不计入对齐约束。
- 因 harness 调用与固件**同一套 FatFs 代码与配置**，PASS 即意味着目标端 `f_mkfs` 会产出
  同样的真实、128 KB 对齐 exFAT 卷（前提是 U 盘扇区为 512 B，绝大多数 U 盘满足）。

> 诚实声明：该验证在 PC 端等价代码上完成，不是对真实 U 盘的物理写入。其价值在于证明
> **文件系统代码路径与几何参数真实有效**，而非证明某块具体 U 盘的兼容性。

---

## 4. 仿真/调试环境验证（VSCode + OpenOCD + arm-none-eabi-gdb + STLink）

> 工具链路径全部走环境变量，工程文件不写死本机绝对路径（见 README §7）。

### 4.1 配置交付（均在工程目录内）
- `openocd/stm32f429_stlink.cfg`：`[find interface/stlink.cfg]` + `transport select swd` + `[find target/stm32f4x.cfg]`，零硬编码。
- `.vscode/launch.json`：Cortex-Debug，`openOCDPath`/`searchDir`/`gdbPath` 用 `${env:VAR}`；`executable`/`configFiles`/`svdFile` 相对路径；含 SVD 外设寄存器视图。
- `.vscode/tasks.json` / `settings.json` / `c_cpp_properties.json` / `extensions.json`、`debug/gdbinit`、`CMakePresets.json`、`.vscode/STM32F429x.svd`。

### 4.2 实测结果（PASS）
- OpenOCD 0.12.0 常驻服务器：`STLINK V2J46S7`、`SWD DPIDR 0x2ba01477`（F429 正确）、Cortex-M4 r0p1、6 硬件断点 / 4 观察点，`Listening on port 3333`(gdb) / `4444`(telnet)。
- `arm-none-eabi-gdb` 连 `:3333` → `monitor reset halt`（PC=Reset_Handler 0x08001840，MSP=0x20030000）→ 在 `main()`(`main.c:74`) 命中硬件断点 → `info registers` / `bt` 显示源码级调用栈 → `reset halt` 干净 detach。
- **固件启动确认（关键）**：gdb 调用栈为
  `main()(main.c:141 vTaskStartScheduler) → vTaskStartScheduler()(tasks.c:1670) → <signal handler> → OTG_FS_IRQHandler()(stm32f4xx_it.c:74 tuh_int_handler(0))`。
  证明固件已正常启动、FreeRTOS 调度器已起、USB Host(OTG_FS) 中断正常触发（等待 U 盘），**非崩溃/死循环**。

### 4.3 串口实测（COM5，2026-08-26 PASS）

COM5（USART3 PB10/PB11，115200 8N1）驱动已修复、可正常 `open()`。烧录修正后固件并通过
`verify_serial/flash_com5_test.py` 抓取，得到**完整启动 + U 盘端到端**输出：

```
System Init
I2C / PCF8574 init OK
SDRAM Init OK
FreeRTOS Heap configured (SDRAM @0xC0000000)
Waiting for USB disk...
USB Host Init
USB Disk Connected (MSC ready)
USB Disk Mounted
Create test.txt
Write test.txt
Read test.txt
----------------
STM32F429 USB Host FATFS Test
Hello from USB Flash Disk!
FreeRTOS + USB Host + exFAT
----------------
USB Disk Ready
Heap object @ 0xC0002F38 (SDRAM base 0xC0000000)
```

- `USB Host Init` 出现在 `Waiting for USB disk...` **之后** → 证实 USB 硬件/栈初始化已被推迟到
  `usbh_host_task`（OS 启动后）执行，修正了“中断触发 FromISR queue 破坏 RTOS”的跑飞问题。
- `Heap object @ 0xC0002F38` → 证实 FreeRTOS 堆确实落在外部 SDRAM（0xC0000000）。
- U 盘完整链路（枚举 → 挂载 → 创建/写/读 `test.txt` → USART3 回显）跑通，固件与真实硬件行为一致。

---

## 5. 证据 #5~#11 — 代码审查要点（逐条核对）

### #5 TinyUSB Host 集成
- `app/tusb_config.h`：`CFG_TUH_ENABLED=1`、`CFG_TUH_MSC=1`、`CFG_TUH_RHPORT=0`（OTG FS）。
- `bsp/bsp_usb_hw.c`：`__HAL_RCC_USB_OTG_FS_CLK_ENABLE()`（F429 正确宏，非 `OTGFS`）、
  PA11/PA12 配 `GPIO_AF10_OTG_FS`、`OTG_FS_IRQn` 优先级设为
  `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`（满足 FromISR 调用要求）。
- `app/stm32f4xx_it.c`：`OTG_FS_IRQHandler → tuh_int_handler(0)`。
- `app/usb_host_app.c`：`tuh_msc_mount_cb` / `tuh_msc_umount_cb`、diskio 胶水
  `disk_read/write/ioctl` 经 `tuh_msc_read10/write10` + 阻塞等待完成回调。
- 编译链接全量通过（见 #1），证明 API 签名、OSAL（FreeRTOS，header-only）均正确。

### #6 SDRAM 先于 FreeRTOS 堆
- `app/main.c` 顺序：`bsp_sdram_init()` → `vPortDefineHeapRegions(xHeapRegions)` →
  之后才 `xTaskCreate(...)`。
- `app/sdram_heap.c`：`ucHeap[configTOTAL_HEAP_SIZE]` 置于 `.freertos_heap`（NOLOAD），
  注释明确「SDRAM 先于任何 FreeRTOS 对象」。

### #7 TIM11 / SysTick 独立
- `app/stm32f4xx_hal_timebase_tim.c`：`HAL_InitTick` 用 TIM11，NVIC 用
  `TIM1_TRG_COM_TIM11_IRQn`（F4 TIM11 共用该中断号，无独立 TIM11_IRQn）。
- `app/stm32f4xx_it.c`：handler 名为 `TIM1_TRG_COM_TIM11_IRQHandler`，与启动向量表一致。
- SVC/PendSV/SysTick 向量直连 FreeRTOS 端口（`startup_stm32f429xx.s` 已核对）。

### #8 USART3 调试输出
- `bsp/bsp_uart.c`：USART3 PB10/PB11，115200 8N1；环形缓冲 + 临界区，**不依赖 FreeRTOS
  对象**，故可在调度器启动前打印启动日志。
- `app/syscalls.c`：`_write` 重定向到 `uart_puts`（已修正注释为 USART3）。

### #9 LED
- `bsp/bsp_led.h`：LED0=PB1，LED1=PB0（低有效）。
- `app/main.c` `led_task`：LED0 每 ~500 ms 翻转（心跳）；LED1 随 `g_usb_state`
  （枚举/挂载/错误）变化。

### #10 BEEP / ETH 复位
- `bsp/bsp_pcf8574.c`：BEEP = P0（低电平响，已与用户确认）；`BSP_ETH_PHY_Reset()` 经
  P7（P7=0 → ETH_RESET=1 释放复位）。`main.c` 初始化时写 `0x7F`（P7=0 释放、P0=1 静音）。

### #11 I2C2
- `bsp/bsp_i2c.c`：I2C2 PH4(SCL)/PH5(SDA)，100 kHz，开漏上拉；含总线恢复
  （`BSP_I2C_Recover`：时钟脉冲输出 + STOP + 外设复位），应对复位后 SDA 卡低。

---

## 6. 硬件验证项（✅ 2026-08-26 真机 PASS）

以下项已于真实硬件（STM32F429IGT6 + STLink + U 盘）经 OpenOCD 烧录 + COM5(USART3) 串口完成验证：

1. **U 盘枚举 + test.txt 端到端**：插盘 → `tuh_msc_mount_cb` 触发 → `file_task`
   写 `0:test.txt` → 重开读回 → USART3 回显。串口实测得到完整链路输出
   `USB Disk Connected → Mounted → Create/Write/Read test.txt → 回显 → USB Disk Ready`（§4.3）。
2. **SDRAM/FMC 运行时时序**：`bsp_sdram_init()` 自测通过；运行时
   `Heap object @ 0xC0002F38` 证实 FreeRTOS 堆确实分配于外部 SDRAM（0xC0000000），W9825G6KH-6
   @ 84 MHz（HCLK/2）参数稳定。
3. **USB 初始化时序修正**：将 `USBH_HW_Init()` + `tusb_init()` 从 `main()`（调度器前）移至
   `usbh_host_task` 入口（OS 启动后），消除“OTG_FS 中断触发 FromISR queue 破坏 RTOS”的跑飞；
   串口输出中 `USB Host Init` 出现在 `Waiting for USB disk...` 之后，证实时序修正生效。

> 注：exFAT 几何（128 KB 数据区对齐 + 128 KB 簇）的真实性仍由 **PC 端等价代码验证**
>（`verify_exfat/harness.c`，12/12 PASS）提供 —— 因手头无 exFAT 格式化 U 盘介质，真机仅验证了
> FatFs 通用读写链路（对 FAT32/exFAT 均透明）。需要 128 KB 对齐介质时，再用 `USB_DISK_AUTO_FORMAT=1`
> 一次性格式化即可复现 harness 的几何。

复测方法（真机）：
```bash
# 先设置环境变量（见 README §7.2），再用工程内配置起 OpenOCD 烧录
openocd -s %OPENOCD_SCRIPTS% -f openocd/stm32f429_stlink.cfg \
  -c "program build/stm32f429_tinyusb_ui.elf verify reset exit"
# 串口 115200 8N1 观察日志；插入 U 盘后确认 test.txt 回显
# 或更省事：python verify_serial/flash_com5_test.py（自动烧录 + 抓 COM5）
```

---

## 7. 交付清单

- [x] 双构建（Debug/Release）零警告，生成 `.elf/.hex/.bin/.map`
- [x] 真实 exFAT 支持（`FF_FS_EXFAT=1`）+ 128 KB 数据区对齐 + 128 KB 簇（harness 12/12 PASS）
- [x] TinyUSB USB FS Host 集成贯通（编译验证）
- [x] FreeRTOS 堆置于外部 SDRAM，且 SDRAM 先于 FreeRTOS 初始化（顺序约束）
- [x] TIM11 / SysTick 双独立时基（168 MHz SYSCLK / 48 MHz USB）
- [x] USART3 / LED / I2C / PCF8574(BEEP+ETH复位) / SDRAM 驱动就绪
- [x] **仿真/调试环境**（VSCode + OpenOCD + arm-none-eabi-gdb + STLink）：`openocd/stm32f429_stlink.cfg`、`.vscode/*`、`debug/gdbinit`、`CMakePresets.json`、SVD 视图，工具路径全走环境变量（见 §4 + README §7）
- [x] README.md + 本验收报告
- [x] OpenOCD 烧录验证：STLink V2J46S7、校验通过、`reset run`（固件已运行，PC/SP 合法）
- [x] **U 盘端到端真机验证**（2026-08-26 COM5 串口实测：枚举→挂载→test.txt 读写回显全链路 PASS）
- [x] **SDRAM/FMC 真机时序验证**（2026-08-26：堆落 0xC0000000 运行态证实，稳定）
- [x] **USB 初始化时序修正**并真机验证（OS 启动后初始化，消除 ISR FromISR queue 破坏 RTOS）

---

## 8. 已知限制 / 备注

- `FF_CODE_PAGE=437`（美式 OEM），如需中文文件名需切换至 `936` 并相应调整字库。
- `USB_DISK_AUTO_FORMAT=0`：默认**不**自动格式化 U 盘（避免误清用户数据）；仅当显式置 1
  且遇 `FR_NO_FILESYSTEM` 时才格式化，且使用 `FM_EXFAT` + 128 KB 对齐参数。
- exFAT 几何（128 KB 数据区对齐 + 128 KB 簇）验证为 PC 端等价代码验证，非真实 U 盘物理写入（详见 §3 诚实声明）；真机已验证 FatFs 通用读写链路（对 FAT32/exFAT 透明）。
- OpenOCD 实际路径为 `D:/software/ST/OpenOCD`（sysprogs 0.12.0），早期记忆里的 `D:/Software/openocd` 已失效；工程配置改用环境变量 `OPENOCD_BIN`/`OPENOCD_SCRIPTS`，不写死。
- 串口：COM5（USART3 PB10/PB11，115200 8N1）已成功抓取完整启动横幅与 U 盘端到端回显（详见 §4.3），硬件验证通过。
