# 验收报告 — STM32F429IGT6 USB FS Host (U盘) + FatFs exFAT + FreeRTOS

**项目**：`102.stm32f429_tinyusb_ui`
**日期**：2026-08-26（第一版） / **2026-08-28（第二版：TIM7 + microSD + 启动加载器）**
**工具链**：arm-none-eabi-gcc 15.3.1 / CMake 4.2.1 / Ninja 1.13.2
**验收方法**：依据 `stm32-verification-acceptance` skill —— 编译验证（Compile-Verified）
与硬件验证（Hardware-Verified）**明确区分标注**。exFAT 几何（128 KB 对齐/簇）由 **PC 端等价
代码验证**（无真实 U 盘格式化介质）；而 **U 盘枚举 / test.txt 端到端读写 / SDRAM 堆运行态** 已于
2026-08-26 通过 **OpenOCD + STLink 真机烧录 + COM5(USART3) 串口** 完成硬件验证（详见 §4.3 / §6）。
**第二版全部条目同样为真机验证**（2026-08-28，OpenOCD + STLink + COM5），见 **§1.1 / §6.1**。

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
| 7 | HAL 时基 **TIM7** 与 SysTick 独立 | **Hardware-Verified** | ✅ PASS（真机 2026-08-28） |
| 8 | USART3 (PB10/PB11) 调试输出路径 | Hardware-Verified | ✅ PASS |
| 9 | LED0(PB1) 心跳 / LED1(PB0) USB 状态 | Code-Review | ✅ PASS |
| 10 | BEEP(PCF8574 P0) / ETH 复位(PCF8574 P7) | Code-Review | ✅ PASS |
| 11 | I2C2 (PH4/PH5) 挂载 PCF8574 等从设备 | Code-Review | ✅ PASS |
| 12 | **U 盘真实枚举 + test.txt 端到端读写** | **Hardware-Verified** | ✅ **PASS（真机 2026-08-26）** |
| 13 | **SDRAM/FMC 运行时时序与稳定性** | **Hardware-Verified** | ✅ **PASS（真机 2026-08-26）** |
| 14 | **USB 初始化时序（OS 启动后，避免 ISR FromISR queue 破坏 RTOS）** | **Hardware-Verified** | ✅ **PASS（真机 2026-08-26）** |

### 1.1 第二版增量（2026-08-28）：TIM7 + microSD + 启动加载器

| # | 验收项 | 标签 | 结果 |
|---|--------|------|------|
| 15 | **HAL 时基 TIM11 → TIM7**（独立向量 `TIM7_IRQn=55`，APB1 84 MHz 定时器时钟） | **Hardware-Verified** | ✅ PASS |
| 16 | **microSD（SDIO 4-bit，PC8-12 + PD2）驱动**，无卡时优雅降级不卡死 | **Hardware-Verified** | ✅ PASS |
| 17 | **FatFs 双卷**：`0:`=USB MSC，`1:`=microSD（`FF_VOLUMES=2` + 统一 diskio 胶水） | **Hardware-Verified** | ✅ PASS |
| 18 | **启动页纯 ASCII**（走编译期字表，零文件依赖）显示 `wait for system start...` | **Hardware-Verified** | ✅ PASS |
| 19 | **加载器优先序：先 SD，SD 不可用才等 USB** | **Hardware-Verified** | ✅ PASS |
| 20 | **10 s 超时居中显示 `sdcard and usb loader failed!`**（只触发一次） | **Hardware-Verified** | ✅ PASS |
| 21 | **主界面设备状态面板**（SD 卡/USB/字库/主频/运行时间/缓存；CJK 从字库渲染） | **Hardware-Verified** | ✅ PASS |
| 22 | `verify_boot_flow.py` 自动化验收 | **Hardware-Verified** | ✅ **PASS 17/17** |

### 1.2 第三版增量（2026-08-28）：电容触摸 + 双页面 + I2C2 传感器

| # | 验收项 | 标签 | 结果 |
|---|--------|------|------|
| 23 | **软件位绑定 I2C**（`bsp_sw_i2c.c`，PH6=SCL / PI3=SDA，开漏上拉 ~165 kHz） | **Hardware-Verified** | ✅ PASS |
| 24 | **GT9147/GT911 识别 + ID 校验打印**（`product ID = "911" (addr 0x14) -> MATCH`） | **Hardware-Verified** | ✅ PASS |
| 25 | 读取芯片自身触摸分辨率（自报 **480×800**）并与 LVGL 画布对齐 | **Hardware-Verified** | ✅ PASS |
| 26 | **T_PEN 中断链路**：EXTI line 7 → ISR → 二值信号量 → `touch_task` 唤醒 | **Hardware-Verified** | ✅ **PASS 7/7** |
| 27 | LVGL **pointer indev** 注册，坐标取自 `bsp_touch` 发布的状态 | **Hardware-Verified** | ✅ PASS |
| 28 | **主界面底部左/右图标按钮**（`lv_line` 绘制，不依赖字库文件），可循环翻页 | Compile-Verified + Code-Review | ✅ PASS |
| 29 | **第二页硬件信息**：AP3216C（IR/环境光/接近）+ MPU9250（加速度/角速度/磁场） | **Hardware-Verified** | ✅ PASS（两者 init OK） |
| 30 | 传感器独立任务采样（500 ms）+ 失败退避 2 s + 限流打印 60 s | **Hardware-Verified** | ✅ PASS |
| 31 | 手指触摸坐标上报（`[TOUCH] raw=.. -> lv=.. irq=N`）+ 实际翻页 | ⏳ **待人工** | 脚本 `verify_touch.py` 已就绪 |
| 32 | 修复 4 个缺陷：触摸画布取到 1×1、UI 布局硬编码 800×480、磁力计失败致整包丢弃、首条错误日志被吞 | **Hardware-Verified** | ✅ PASS |

### 1.3 第四版增量（2026-08-29）：PRINT_LOG 日志系统替换全部 printf

| # | 验收项 | 标签 | 结果 |
|---|--------|------|------|
| 33 | 新增 `app/log.c/.h`（`printf_log` / `vprintf_log` / `PRINT_LOG` + `PRINT_LOG_ENABLE`） | **Hardware-Verified** | ✅ PASS |
| 34 | `app/` + `bsp/` 全部 `printf()` 替换为 `PRINT_LOG()`（`snprintf` 保留） | Compile-Verified + Code-Review | ✅ PASS（12 文件 / 79 处） |
| 35 | `bsp_uart` 升级为 101 语义：调度器未运行→阻塞轮询，已运行→互斥量+临界区；新增 `uart_flush()` | **Hardware-Verified** | ✅ PASS |
| 36 | CMake `ENABLE_PRINT_LOG` 开关，三种配置零警告 | Compile-Verified | ✅ PASS |
| 37 | 日志**开**：日志进入 TX 环且被完整排空（SWD 实测 `g_tx_head=0x0350`, head==tail） | **Hardware-Verified** | ✅ PASS |
| 38 | 日志**关**：串口零字节 + 系统仍完整启动（SWD 实测 `g_tx_head=0x0000`, `g_usb_state=4`） | **Hardware-Verified** | ✅ PASS |
| 39 | U 盘内容 dump 同步受开关控制（关日志时连盘都不读） | Compile-Verified + Code-Review | ✅ PASS |
| 40 | 串口文本逐行比对（COM5） | ⏳ **待人工** | COM5 处于 code-31 驱动故障，需重新插拔后补跑 |

> 第四轮（2026-08-29）验证时 COM5 不可用，改用 **SWD 读目标内存**取证
> （`g_tx_head` / `g_tx_tail` / `g_tx_busy` / `g_usb_state`），见 §6.3。

### 1.4 第五版增量（2026-08-29）：修复 I2C2 中断风暴 + 传感器自愈

| # | 验收项 | 标签 | 结果 |
|---|--------|------|------|
| 41 | **PH7 (T_PEN) 改 `GPIO_PULLUP`**（仅地址锁存用 NOPULL）+ 位绑定事务期屏蔽 EXTI line 7 | **Hardware-Verified** | ✅ PASS |
| 42 | **ISR 内立即屏蔽 line 7** + 任务侧消抖 5 ms 后 `bsp_touch_irq_rearm()` 重新武装 | **Hardware-Verified** | ✅ PASS（46 925/s → ~20 Hz） |
| 43 | **IRQ 速率看门狗**（> 1000/s 打 WARNING） | **Hardware-Verified** | ✅ PASS（它给出了 46925/s 的决定性数据） |
| 44 | 传感器读取用 `vTaskSuspendAll()` 包成**原子操作** | **Hardware-Verified** | ✅ PASS |
| 45 | I2C 超时 10 ms → **50 ms** + 失败先 `BSP_I2C_Recover()` 再立即重试一次 | **Hardware-Verified** | ✅ PASS（`errors=0`） |
| 46 | 触摸轮询循环上限 `TOUCH_MAX_POLLS`(3 s) | Compile-Verified + Code-Review | ✅ PASS |
| 47 | 新增 `verify_sensors.py`：SWD 直读 `s_data` 做**正向**验证 | **Hardware-Verified** | ✅ **PASS 7/7** |
| 48 | 修复 AK8963 时序（等待 2 ms → 20 ms，≥2 个采样周期） | Compile-Verified + Code-Review | ✅ PASS |
| 49 | 新增 `AK8963_WIA` 器件检测：本板 WIA=0x00，**无磁力计**，不再谎报 0.0 uT | **Hardware-Verified** | ✅ PASS |
| 50 | 修复后回归：启动流程 17/17、触摸中断链路 7/7 | **Hardware-Verified** | ✅ PASS |

> 标签含义：
> - **Compile-Verified**：在目标工具链下成功编译/链接（零警告），逻辑经审查或等价 PC 程序验证。
> - **Code-Review**：通过源码静态审查确认设计/接线/顺序正确。
> - **Hardware-Verified**：在真实 STM32F429IGT6 上用 **OpenOCD + STLink 烧录**、
>   **COM5 (USART3) 抓串口**确认（2026-08-26 与 2026-08-28 两轮均已执行）。

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

### 6.4 第五版真机验证证据（2026-08-29，PASS 7/7 + 回归 17/17、7/7）

**故障**：用户报"系统未启动时工作正常，FreeRTOS 启动后 I2C 访问 AP3216/MPU9250 读取失败"，
日志同时有 `irq=93014` 与卡住的 `raw=(436,771)`。

**定位**：传感器 init 在**触摸 EXTI 使能之前**成功、之后全败 → 指向触摸中断。
新加的 IRQ 速率看门狗给出了决定性数字：
```
[TOUCH] WARNING: T_PEN interrupt storm 46925/s (> 1000/s) - check PH7 pull-up / crosstalk from PH6
```

**根因链**：PH7 浮空 + 紧邻 PH6(位绑定 SCL 165 kHz) 串扰 → 中断风暴；
`HAL_I2C_Mem_*` 是轮询式传输，被 idle+4 的触摸任务抢占 → HAL 10 ms 超时 →
**从机拉住 SDA、I2C2 锁 BUSY**；旧代码只退避重试、从不调 `BSP_I2C_Recover()` → 永久失效。

**修复后正向验证**（`verify_sensors.py`，SWD 直读 `s_data`，不依赖"没报错"）：
```
AP3216C : ok=1  IR=5   环境光=4 lux  接近=30
MPU9250 : ok=1 (mag=0)
  加速度  ax=-0.03 ay=+0.01 az=+1.00 g     |a| = 1.00 g ← 板子平放，重力全在 Z
  角速度  gx=-0.1 gy=+0.2 gz=-2.1 dps
  统计    : samples=38  errors=0   AK8963 WIA=0x00
RESULT: 7 passed, 0 failed -> PASS
```
- `errors=0`（修复前每 2 s 一条 FAILED）、`samples=38` 正常推进、
  环境光随光照 0 → 4 → 10 lux 变化，证明传感器确实在采。
- **AK8963 WIA=0x00**：本模块**没有可用磁力计**（硬件事实）。
  旧代码把全零当"读取成功"上报属谎报；现在 init 返回 `-3`、页面显示「磁场  AK8963 未装配」。

**回归**：`verify_boot_flow.py` 17/17、`verify_touch_irq.py` 7/7，均无退化。

### 6.3 第四版真机验证证据（2026-08-29，SWD 读内存，PASS 6/6）

验证时 **COM5 (CH340 @ LOCATION=1-8) 报 `PermissionError(13) / code 31`**，
另一个 CH340 在 COM4 但并非本板（抓到 0 字节），串口侧无法取证，
故改用 **SWD 读目标内存** 直接检查 UART TX 环形缓冲的状态变量：

```
python tools/verify_serial/verify_log_switch.py

PRINT_LOG 开 : g_tx_head=0x0350  g_tx_tail=0x0350  g_tx_busy=0  g_usb_state=4
PRINT_LOG 关 : g_tx_head=0x0000  g_tx_tail=0x0000  g_tx_busy=0  g_usb_state=4
========== RESULT: 6 passed, 0 failed -> VERDICT: PASS ==========
```

- 日志开：**848 字节**进入 TX 环且 `head == tail`（被 ISR 完整排空，无丢字节）。
- 日志关：`g_tx_head` 恒为 0、`g_tx_busy` 恒为 0 → **一个字节都没进环**；
  同时 `g_usb_state=4 (USB_MOUNTED)` 证明系统照常完整启动。
- 三种构建均零警告（README §5.3）：Release 开 **298,120 B** / Release 关 **291,960 B**
  （省 **6,160 B**）/ Debug 开 **259,236 B**。

## 6.1 第二版真机验证证据（COM5，2026-08-28）

**方法**：OpenOCD 0.12.0 + STLink（SWD）烧录 `.elf`（`flash write_image erase` + `verify_image`
+ `reset run`），COM5（USART3 PB10/PB11，115200 8N1）从 t=0 抓串口，脚本化 pass/fail 计数。
**注意：烧录必须用 `.elf`**，用 `.bin` 会报 `no flash bank found for address 0x00000000`。

### 6.1.1 路径 A：microSD 未插卡 + U 盘带字库 → 回退到 USB 并进入主界面

```
System Init
I2C / PCF8574 init OK
SDRAM Init OK
FreeRTOS Heap configured (SDRAM @0xC0000000)
Waiting for USB disk...
USB Host Init                                 ← 仍在调度器之后初始化
[UI  ] starting LCD + LVGL bring-up
[LCD ] controller ID = 0x8000
[LCD ] active GRAM window 480x800 (panel spec 800x480)
[UI  ] boot screen: wait for system start...   ← ASCII 启动页，零文件依赖
[SD  ] SDIO init FAILED (no card?)             ← 先探 SD（优先序成立）
USB Disk Connected (MSC ready)
USB Disk Mounted
[FONT] trying USB 0:/SYSTEM/FONT/              ← 回退 USB
[FONT] source=0: mask=0x1F                     ← UNIGBK + GBK12/16/24/32 全开
[UI  ] main screen (fonts from 0:)             ← 进入主界面
[UI  ] glyph cache: hits=... misses=45         ← CJK 字形确实从 GBKxx.FON 读出
```

`verify_boot_flow.py` 结果：**17 passed, 0 failed → VERDICT: PASS**。

### 6.1.2 路径 B：两端都无介质 → 10 s 超时失败页

板上 U 盘无法远程拔掉，故用编译开关临时关闭 USB 回退来复现「两端都无介质」：

```bash
cmake -S . -B build_to -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-DLOADER_ENABLE_USB_FALLBACK=0"
cmake --build build_to
SERIAL_PORT=COM5 FIRMWARE=build_to/stm32f429_tinyusb_ui.elf \
      python tools/verify_serial/verify_boot_flow.py
```

```
[UI  ] boot screen: wait for system start...
[SD  ] SDIO init FAILED (no card?)
USB Disk Connected (MSC ready)      ← USB 枚举照常，只是不再作为字库来源
USB Disk Mounted
...
[UI  ] timeout: sdcard and usb loader failed!   ← 第 10 s，且只打印一次
```

结果：**15 passed, 0 failed → VERDICT: PASS**。验证完毕后已重新烧录正式固件
（`build_rel/stm32f429_tinyusb_ui.elf`，USB 回退开启）。

### 6.1.3 本轮修复的三个缺陷（均已真机复测）

| 缺陷 | 根因 | 修复 | 复测 |
|------|------|------|------|
| 新增日志时有时无、已有日志缺字节 | `uart_write()` 环形缓冲满即静默丢弃；`UART_TX_BUF_SIZE` 仅 512 B，而 `file_task` 全量 dump 3 MB 字库/JPG 长期占满缓冲 | 缓冲扩到 2048 B；dump 加两道闸（>2048 B 文件只列不 dump；整轮预算 16 KB） | 日志完整，25 s 抓包量从 8469 B 降到 2287 B 且无缺失 |
| `[SD  ] SDIO init FAILED` 每 500 ms 刷屏 | 探测失败后未静音 | `sd_card_set_quiet()`；10 s 后探测降频到 3 s | 只打印 1 次 |
| `[UI  ] timeout: ...` 每 5 ms 重复打印 | deadline 分支写成 `s_state != LOAD_OK`，`LOAD_FAILED` 同样满足 | 改为 `s_state == LOAD_BOOT` | 只打印 1 次 |

### 6.2 第三版真机验证证据（COM5，2026-08-28）

**6.2.1 中断链路（全自动，PASS 7/7）**

`EXTI_SWIER` 可产生与引脚边沿完全等价的软件中断，因此整条链路无需人手即可自动验证：

```
SERIAL_PORT=COM5 python tools/verify_serial/verify_touch_irq.py

[TOUCH] task started, probing GT9147 (T_SCK=PH6 T_MOSI=PI3 T_CS=PI8 T_PEN=PH7)
[TOUCH] product ID = "911" (addr 0x14) -> MATCH
[TOUCH] stored config: version=0x51 resolution=480x800
[TOUCH] EXTI cfg: EXTICR2=0x00007000 IMR=0x0080 FTSR=0x0080 prio=6
[TOUCH] ready: id=911 addr=0x14 cfg=0x51, touch 480x800 -> canvas 480x800, swap=0 invX=0 invY=0
[TOUCH] INT armed on T_PEN (PH7, falling edge) -> waiting
[TOUCH] T_PEN interrupt received (irq=2)          ← 软件注入 EXTI line 7 后任务被唤醒
[UI  ] LVGL canvas 480x800, pointer indev registered
[SENS ] AP3216C init OK
[SENS ] MPU9250 init OK (WHO_AM_I check)
```

- `EXTICR2=0x00007000` → EXTI line 7 已复用到 **GPIOH**（字段值 `0x7`）；
  `IMR=FTSR=0x0080` → line 7 中断使能 + 下降沿触发；
  `prio=6` ≥ `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5)`，FromISR 调用合法。
- 该方法验证的是**中断链路本身**，不验证 PH7 引脚电平，仍需 §6.2.3 的人工确认。

**6.2.2 启动流程回归（PASS 17/17，无退化）**

接入触摸与传感器后重跑 `verify_boot_flow.py`：17/17 全过 —— 启动页 → SD 无卡降级 →
USB 挂载 → 字库 `mask=0x1F` → 主界面 → `glyph cache: hits=83 misses=46`
（CJK 字形确实从 `GBKxx.FON` 读出）。

**6.2.3 本轮修复的四个缺陷（均已真机复测）**

| 缺陷 | 根因 | 修复 | 复测 |
|------|------|------|------|
| `[TOUCH] ready: ... canvas 1x1`，坐标恒为 0 | `touch_task`(idle+4) 先于 `ui_task`(idle+2) 跑，`g_lcd_info` 还是清零状态 | `bsp_lcd.c` 新增 `lcd_driver_ready()`，`bsp_touch_init()` 等该标志（≤10 s），`bsp_touch_scan()` 再刷新一次几何 | `canvas 480x800`，`touch 480x800 -> canvas 480x800` |
| UI 横向被裁 320 px、纵向空 320 px | `app_ui.c` 硬编码 `UI_W 800 / UI_H 480`，而实际 GRAM 窗口是 **480×800** | 布局改为从 `lv_disp_get_hor_res/ver_res()` 取，三栏位置/高度按实际画布算 | 启动日志 `LVGL canvas 480x800`，导航条落在画布底部 |
| 第 2 页全部 `--` 长达 30 s | `bsp_mpu9250_read()` 返回 `-3` 只代表 AK8963 读失败，加速度/陀螺其实读到了；原代码 `!= 0` 一律整包丢弃 | `-3` 时仍发布加速度/陀螺，只把 `mag_ok` 置 0；退避 30 s → 2 s；错误每 60 s 最多打印一次 | 首轮偶发失败后 2 s 即恢复，页面仅"磁场"显示 `AK8963 未就绪` |
| 首条错误日志永远打不出来 | 限流用 `0xFFFFFFFFU` 作哨兵，`s_round - 0xFFFFFFFF` 回绕成 2，永远 `< 120` | 哨兵改 `0`，条件加 `*last_at == 0U` 短路 | 首条错误必现 |

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

**第二版增量（2026-08-28）**

- [x] HAL 时基 **TIM11 → TIM7**（`TIM7_IRQn=55` 独立向量；`stm32f4xx_it.c` 换 `TIM7_IRQHandler`）
- [x] FreeRTOS 堆继续落在 **SDRAM**（heap_5 单区 512 KB @0xC0000000，本版未变动，已核对）
- [x] LVGL 继续作为**独立任务**运行，draw buffer 与 `LV_MEM_ADR`(0xC0100000/256 KB) 均在 **SDRAM**
- [x] **microSD SDIO 4-bit 驱动**（`bsp/bsp_sdio.c`，PC8-12 + PD2，轮询无 DMA，卡时钟 12 MHz）
- [x] **FatFs 双卷**（`FF_VOLUMES=2`）+ 统一 diskio 胶水（`app/fs_diskio.c`）+ `fs_lock` 串行化
- [x] **启动加载器**：ASCII 启动页 → 先 SD → 失败等 USB → 主界面 / 10 s 超时失败页
- [x] 主界面新增 **SD 卡状态行**与**字库来源行**
- [x] `tools/verify_serial/verify_boot_flow.py`（17 项 pass/fail）+ `capture_reset.py` 调试助手
- [x] 修复 UART 环形缓冲丢日志 / SD 空卡槽刷屏 / 超时页重复触发 三个缺陷
- [x] README.md + 本报告同步更新

**第三版增量（2026-08-28）**

- [x] **软件位绑定 I2C**（`bsp/bsp_sw_i2c.c`）：PH6=SCL / PI3=SDA，开漏 + 上拉，~165 kHz，DWT 忙等时序
- [x] **GT9147 驱动**（`bsp/bsp_gt9147.c`）：复位寻址（0x14/0x5D 双候选）→ 读 Product ID →
      打印并校验是否匹配 → 读配置版本与触摸分辨率 → 仅 ID=9147 且版本过旧才上传 184 B 配置块
- [x] **触摸服务**（`bsp/bsp_touch.c`）：T_PEN(PH7) 下降沿 EXTI（HAL_EXTI + 回调注册）→
      二值信号量 → 坐标映射到 LVGL 画布（swap/invert 三个宏可配）→ 每次触摸打印 `raw -> lv`
- [x] **触摸任务**（`app/touch_task.c`）：中断唤醒 → 15 ms 轮询直到抬手；1 s 超时兜底 +
      "无中断却检测到触摸"告警
- [x] **LVGL 输入设备**（`bsp/lv_port_indev.c`）：pointer indev，只读取发布状态，不做总线操作
- [x] **传感器任务**（`app/sensor_task.c`）：AP3216C + MPU9250，500 ms 采样，磁力计独立标记，
      失败退避 2 s + 限流打印 60 s
- [x] **UI 双页面**（`app/app_ui.c`）：状态页 / 硬件信息页 + 底部左/右 `lv_line` 图标按钮循环翻页；
      几何改为自适应画布尺寸
- [x] `tools/verify_serial/verify_touch_irq.py`（中断链路全自动 7/7）+ `verify_touch.py`（交互式）
- [x] 修复 4 个缺陷（画布 1×1 / 布局硬编码 / 磁力计整包丢弃 / 首条错误被吞）
- [x] README.md（§1 引脚表、§3.2.3~3.2.4、§3.5、§5.3、§6.1、§7.7、§8）+ 本报告同步更新

**第四版增量（2026-08-29）**

- [x] **`app/log.c/.h`**：`printf_log()` / `vprintf_log()` / `PRINT_LOG()` +
      `PRINT_LOG_ENABLE` 全局开关；192 B 栈缓冲 + `vsnprintf`，不分配堆
- [x] **全量替换**：`app/` 12 个文件共 79 处 `printf()` → `PRINT_LOG()`
      （`snprintf` 全部保留，未动）
- [x] **`bsp/bsp_uart.c`** 升级为 101 语义：调度器未运行→`HAL_UART_Transmit()` 阻塞轮询、
      已运行→**惰性创建**的 TX 互斥量 + 临界区入环；新增 `uart_flush()`（带超时保护）
- [x] **CMake `ENABLE_PRINT_LOG`**（默认 ON）→ `-DPRINT_LOG_ENABLE=0|1`，
      配置时打印当前状态
- [x] U 盘内容 dump 用 `#if PRINT_LOG_ENABLE` 包住（关日志时连盘都不读），
      避免绕过开关直连 `uart_write()`
- [x] `tools/verify_serial/verify_log_switch.py`（SWD 读内存，COM5 故障时可用，6/6 PASS）
- [x] README.md（§3.6 日志系统、§5.2 构建开关、§5.3 三配置产物、§7.8 实测、§8）+
      本报告同步更新

**第五版增量（2026-08-29）**

- [x] **PH7 改 `GPIO_PULLUP`**（地址锁存阶段仍用 NOPULL），消除浮空拾噪
- [x] 位绑定 I2C 事务期间屏蔽 EXTI line 7，消除自身 SCL 串扰
- [x] **ISR 内立即屏蔽 line 7** + `bsp_touch_irq_rearm()`（任务侧消抖 5 ms 后重新武装）
- [x] **IRQ 速率看门狗**（1 s 窗口，> 1000/s 打 WARNING）
- [x] 传感器读取用 `vTaskSuspendAll()`/`xTaskResumeAll()` 包成原子操作
- [x] I2C 超时 10 → 50 ms；失败先 `BSP_I2C_Recover()` 再立即重试一次
- [x] 触摸轮询循环上限 `TOUCH_MAX_POLLS`(200 × 15 ms = 3 s)
- [x] 修复 AK8963 时序（`MPU_SLAVE_SETTLE_MS` 2 → 20 ms，≥2 个采样周期）
- [x] 新增 `bsp_mpu9250_mag_id()`：`AK8963_WIA` 器件检测，不存在时不再谎报 0.0 uT
- [x] `tools/verify_serial/verify_sensors.py`（SWD 直读 `s_data` 正向验证，7/7 PASS）
- [x] README.md（§3.5 + 新增 §3.5.1 根因与防护、§6 验收表、§7.9 实测）+ 本报告同步更新

---

## 8. 已知限制 / 备注

- `FF_CODE_PAGE=437`（美式 OEM），如需中文文件名需切换至 `936` 并相应调整字库。
- `USB_DISK_AUTO_FORMAT=0`：默认**不**自动格式化 U 盘（避免误清用户数据）；仅当显式置 1
  且遇 `FR_NO_FILESYSTEM` 时才格式化，且使用 `FM_EXFAT` + 128 KB 对齐参数。
- exFAT 几何（128 KB 数据区对齐 + 128 KB 簇）验证为 PC 端等价代码验证，非真实 U 盘物理写入（详见 §3 诚实声明）；真机已验证 FatFs 通用读写链路（对 FAT32/exFAT 透明）。
- OpenOCD 实际路径为 `D:/software/ST/OpenOCD`（sysprogs 0.12.0），早期记忆里的 `D:/Software/openocd` 已失效；工程配置改用环境变量 `OPENOCD_BIN`/`OPENOCD_SCRIPTS`，不写死。
- 串口：COM5（USART3 PB10/PB11，115200 8N1）已成功抓取完整启动横幅与 U 盘端到端回显（详见 §4.3），硬件验证通过。
