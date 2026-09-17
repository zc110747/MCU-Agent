---
name: stm32-peripheral-drivers
description: STM32 外设驱动速查表与实测踩坑：STM32H743 / STM32F429 引脚映射、OV5640(DCMI) 多缓冲采集、ST7789(SPI6) OLED 显示、SD 卡 FatFs + GBK 中文点阵字库、QSPI(W25Q64) Flash、USB OTG_FS 的 VDD33USB 供电坑、LAN8720A(RMII) 网络、I2C 总线锁死恢复、emWin(STemWin) GUI 栈、USB Host + exFAT U 盘、USART1 非阻塞日志输出（环形缓冲 + TXE 中断）。
agent_created: true
---

# STM32 外设驱动速查与实测踩坑

已验证的两套硬件：STM32H743ZIT6（核心板，HSE 25MHz 无源晶振）与
STM32F429IGT6（HSE 25MHz）。所有引脚均来自真机原理图，可直接复用。

## 一、STM32H743ZIT6 引脚速查

| 功能 | 引脚 | 备注 |
|---|---|---|
| 调试串口 USART1 | PA9(TX)/PA10(RX) | 115200 8N1，ST-Link 虚拟串口（端口号依本机分配，如 COMx） |
| LED | PG7 | 低电平点亮（状态灯） |
| USB FS | PA11(D-)/PA12(D+) | OTG_FS 内部全速 PHY；HS 走 PB14/PB15 |
| QSPI(W25Q64) | CS=PG6/AF10, CLK=PF10/AF9, IO0=PF8/AF10, IO1=PF9/AF10, IO2=PF7/AF9, IO3=PF6/AF9 | 4 线 |
| SDMMC1(4-bit) | CK=PC12, CMD=PD2, D0=PC8, D1=PC9, D2=PC10, D3=PC11 | 逻辑盘 `1:`（裸机）/ `SD:`（Zephyr） |
| SPI6(OLED ST7789) | SCK=PG13/AF5, MOSI=PG14/AF5, CS=PG8(软), DC=PG15(软), BL=PG12(软) | 4 线 SPI，240×240 |
| DCMI(OV5640) | HSYNC=PA4, VSYNC=PG9, PCLK=PA6, D0-7=PC6/PC7/PG10/PG11/PE4/PD3/PE5/PE6 | I2C_SCL=PF14, I2C_SDA=PF15, PWDN=PF13(低有效) |

**H7 系统时钟**：HSE 25MHz → PLL1 → SYSCLK 480MHz，HCLK 200MHz，APB 100MHz。

## 二、STM32F429IGT6 引脚速查

| 功能 | 引脚 | 备注 |
|---|---|---|
| 调试串口 USART1 | PA9(TX)/PA10(RX) | 115200 8N1（端口号依本机分配，如 COMx） |
| LED | PB0/PB1 | 低电平点亮（PB0=受控，PB1=心跳） |
| BEEP | PCF8574T P0 | 低电平发声 |
| SDMMC(SDIO) | SCK=PC12, CMD=PD2, D0-3=PC8-11 | 4-bit |
| ETH(LAN8720A) | MDC=PC1, RXD0=PC4, RXD1=PC5, REF_CLK=PA1, MDIO=PA2, CRS_DV=PA7, TX_EN=PB11, TXD0=PG13, TXD1=PG14 | RMII，PHY addr 0 |
| ETH_RESET | PCF8574T P7 | 高电平正常工作（经三极管反相） |
| I2C(共用 PH5/PH4) | SDA=PH5, SCL=PH4 | 挂 PCF8574T(0x20)/AP3216C(0x1E)/MPU9250(0x68)/AT24C02(0xA0) |
| FMC SDRAM(W9825G6KH-6) | 16bit 数据 D0-15、地址 A0-12、BA0-1、SDNWE=PC0、SDNCAS=PG15、SDNRAS=PF11、SDNE0=PC2、SDCKE0=PC3、SDCLK=PG8 | Bank1 @0xC0000000，32MB |

**F4 系统时钟**：HSE 25MHz → PLL M=25 N=360 P=2 Q=8 → 180MHz（OverDrive + FLASH_LATENCY_5）。
**SRAM 边界**：连续 SRAM 仅 192K（0x20000000~0x2002FFFF），CCM 64K @0x10000000 不连续、
ETH/DMA 访问不到。

## 三、OV5640 (DCMI) 摄像头采集

- 多缓冲机制（双/三缓冲）避免撕裂：图像缓冲放 `0x24000000`（H7 的 RAM_D2/AHBSRAM），
  采集与显示/AI 解耦。
- 分辨率裁剪：人脸检测只需 96×96（先裁剪再 AI）；UVC 满足 240×240 即可。
- **提供已验证驱动源码**（`drv_dcmi.c/.h`、`drv_dcmi_ov5640.c/.h`）比让 AI 从零生成参数稳得多。
- I2C 写 OV5640 寄存器（SCCB 协议，兼容 I2C）；PWDN 低电平工作。

### 3.1 H7 DCMI 寄存器地址与极性（真机踩坑）
- **DCMI 外设基地址 = `0x48020000`（AHB2）**，不是 `0x40050000`（那是 STM32F4 的 DCMI 地址，
  H7 上读错会浪费大量时间）。DMA 的 `PAR`（外设地址寄存器）可用交叉验证。
- **DCMI 极性**：PCK/VSYNC/HSYNC = `RISING / LOW / LOW`，对应传感器寄存器 `0x4740 = 0x21`。
- 传感器输出 `400×300 YUV422/YUYV`，DCMI 再 crop 到目标（如 240×240 / 192×192）。
- DMA：`DMA2_Stream1`（或 Stream7）`CIRCULAR` + `WORD` 对齐 + `FIFO FULL`，`INC4/SINGLE`。
- **彩条模式 `0x503D=0x80`** 是判断 DVP 故障段的最快方法（区分「传感器不输出」与「DCMI 不采样」）。
- 撕裂根因多是在 DMA CIRCULAR 覆写缓冲的任意相位做 `memcpy`；用「采集/显示第三缓冲」结构保证
  （采集侧硬件双缓冲 + 一块仅 CPU 写的显示缓冲）可从结构上杜绝（详见 `stm32-verification-acceptance` 多缓冲验收）。

### 3.2 OV5640 BSP 参考驱动的已知 bug
ST BSP 组件驱动 `BSP/ov5640/` 的 QVGA 表有 bug（水平 binning 未使能），优先用用户提供的
参考驱动 `ov5640_ref.c`；AI 从零生成的参数常卡很久，直接给源码最稳。

## 四、ST7789 (SPI6) OLED 显示

- 4 线 SPI：SCK/MOSI 硬件 SPI6，CS/DC/BL 用 GPIO 软件控制。
- 240×240 分辨率；CS/DC/BL 在 H7 上是 PG8/PG15/PG12。
- **Zephyr 移植关键差异**：Zephyr 的 st7789v 驱动初始化**不发 DISPON(0x29)**，面板保持
  sleep-in 全黑，应用必须调 `display_blanking_off()` 点亮（裸机 init 末尾有 0x29）。
- 软件复位延时：用户板无硬件 RST，Zephyr 走 SWRESET 后仅延 5ms（远小于 ST7789 复位
  ~120ms），需 patch `display_st7789v.c` 加 `k_sleep(K_MSEC(120))`（west update 会覆盖，重装需重打）。

## 五、SD 卡 + FatFs + GBK 中文点阵字库

### 5.1 字库文件（放 SD 卡 `SYSTEM/FONT/`）
| 文件 | 作用 |
|---|---|
| `UNIGBK.BIN` | Unicode→GBK 映射表（4 字节/记录，**双段结构**） |
| `GBK12/16/24/32.FON` | 12/16/24/32 点阵（MSB 优先、列扫描） |

### 5.2 UNIGBK.BIN 双段结构（真机验证，极易踩坑）
- **段 1**（约前 21792 条）：`[unicode_lo, unicode_hi, gbk_lo, gbk_hi]` 小端，**按 Unicode 升序**。
- 1 条全 0 填充记录。
- **段 2**（其余）：`[gbk_lo, gbk_hi, unicode_lo, unicode_hi]` 小端，**按 GBK 升序**。
- ⚠️ 两段排序键不同，**不能对整个文件做 Unicode 二分查找**；初始化时定位段 1 边界
  （第一条全 0 记录）后仅在段 1 内二分。
- ⚠️ GBK 字段在文件里也是小端 `[gbk_lo,gbk_hi]`，函数返回时需交换成常规顺序，
  否则汉字错位（实测"时/钟"显示成"笔/又"）。

### 5.3 渲染路径（LVGL 自定义字体桥）
1. UTF-8 → Unicode 码点（LVGL 解码）。
2. Unicode → GBK：UNIGBK 段 1 二分（注意小端交换）。
3. GBK → 原始点阵：偏移 `190*(qh-0x81) + (ql-0x40或0x41)`，原始 **MSB 优先、列扫描**。
4. 转置：列扫描 → 行扫描（保持 MSB 优先），得到 LVGL 1bpp 行优先位图。
5. ASCII 用移植的 8×16 / 12×24 点阵回退（避免 Montserrat 小字号发虚）。

### 5.4 卷名差异
- 裸机 FatFs：逻辑盘 `1:`。
- Zephyr FatFs：`FF_STR_VOLUME_ID=1` + `CONFIG_SDMMC_VOLUME_NAME="SD"`，必须用 `SD:`，
  用 `1:` 挂载失败。

### 5.5 中文字符编码坑（真机验证）
- FatFs `FF_CODE_PAGE` 必须 `936`（GBK），**绝不可改 437**（否则中文长文件名/字库路径乱码）。
- 文本渲染前用 `utf8_is_valid()` 识别"无 BOM 的合法 UTF-8"，避免把 GBK 字节误当 UTF-8 转码
  （GBK 双字节高字节 0x81–0xFE 常被误判 UTF-8 续字节 → 乱码）。
- 有 BOM 的 UTF-8（`EF BB BF`）走快路径不转码。

### 5.6 SDIO(4-bit) 目标缓冲必须 4 字节对齐
HAL 内部以 `uint32_t*` 读 SDIO FIFO，未对齐地址会 HardFault。`fs_diskio.c` 对未对齐地址统一经
`s_sd_scratch[512]` 中转，切勿删除；应用层传缓冲也尽量保证 4 字节对齐。
- SDIOCLK=48MHz（PLLQ=7，与 USB 同源），ClockDiv=2 → 卡时钟 12MHz，**轮询不接 DMA** 更稳。
- FatFs 卷：`1:`（裸机）/ `SD:`（Zephyr）；`FF_VOLUMES=2` 时 U 盘为 `0:`、SD 卡为 `1:`。
- `ffconf.h` 须 `FF_FS_EXFAT 1`（U 盘 exFAT 才能挂载字库）。

## 六、QSPI (W25Q64) Flash

- H7 的 QUADSPI 外设，4 线；CS/CLK/IO0-3 见 §一。
- 用途：存放字库/网页/模型权重等大数据，释放内部 Flash。
- 注意 QSPI 映射模式（memory-mapped）下只读；写需退出映射模式走命令序列。

## 七、USB OTG_FS 设备枚举不上（H7 最高频坑）

**根因：VDD33USB 供电未就绪**。H7 的 OTG 收发器由 VDD33USB 域供电，必须：
1. 使能片内 LDO 生成（USBREGEN），**或**板子把 VDD33USB 直连外部 3.3V（此时关 REGEN）。
2. **必须** `HAL_PWREx_EnableUSBVoltageDetector()` 且等 `PWR_FLAG_USB33RDY` 就绪。

```c
#if DAP_USB_INTERNAL_REGULATOR
  PWR->CR3 |= PWR_CR3_USBREGEN;
#endif
HAL_PWREx_EnableUSBVoltageDetector();
while (!__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY)) { }
```
- 上电心跳定位：完全不闪=没启动（查 HSE）；闪 3 下后停=卡 USB 供电；闪 3 下后常亮=已枚举。
- USB 座接错控制器：默认 OTG_FS(PA11/PA12)，若实际连 PB14/PB15 需改用 HS 预设。
- 换能传数据的线（很多充电线只有 VBUS+GND）；避免 Hub；CMSIS-DAP v1 是 HID 免驱。

### 7.1 CMSIS-DAP 探针接线（自研探针）
自研 CMSIS-DAP v1 探针（H7 核心板 + TinyUSB HID）访问目标时，SWD 目标线接到：
SWCLK=PA0 / SWDIO=PA1 / nRESET=PA2 / SWO=PA3 / 闲置=PA5 / 目标供电检测=PA7。
**三处线勿混**：烧写线 / USB 上行线 / SWD 目标线。SWD 时序延时不可用 Keil `__asm` `_DELAY`，
改用 DWT 周期计数（`DELAY_SLOW_CYCLES=1` 级）做精确延时。

## 八、STM32H7 内部 Flash 升级引擎（Bootloader 实战坑）

H7 内部 Flash 是**双 Bank（16×128KB）**。做 Bootloader 升级时，DTCM 不可执行、双 Bank 取指、
RWW 语义、跳转 App 序列各有一条独立铁律，任一条错了都是"擦写函数一跑就死"。
完整六条（含可直接抄的 HAL 原语调用序列）见 `references/h7-flash-bootloader.md`。

## 九、emWin (STemWin) GUI 栈（H7 + ST7789，无 OS）

STemWin（Segger emWin 的 ST 版）是 LVGL 之外的另一套 GUI 方案，可 1:1 复刻同一套
H7 + ST7789 OLED 信息面板。

- **预编译库必须 binutils < 2.44**（见 `stm32-ai-dev-environment` 六）：`STemWin_CM7_wc16.a`
  不能塞进 `add_executable` 源列表（CMake 会静默丢弃 `.a`），改为
  `add_library(stemwin STATIC IMPORTED)` +
  `target_link_libraries(... -Wl,--start-group stemwin -Wl,--end-group)`（处理循环引用）。
- **`GUI_Init()` 死循环根因**：STemWin 入口用硬件 **CRC 外设**做完整性校验，未使能 CRC 时钟
  → 校验永远失败 → 死循环。修复：`GUI_Init()` 之前 `__HAL_RCC_CRC_CLK_ENABLE()`
  （CRC 在 AHB4，`RCC->AHB4ENR` 的 `CRCEN`）。
- **颜色顺序**：`GUI_USE_ARGB` 默认 0 时 `GUI_COLOR` 为 `0x00BBGGRR`（蓝在高字节），
  与 LVGL 的 `0x00RRGGBB` 相反。传给 `GUI_SetColor()` 前做一次 R/B 交换，保持像素一致。
- **显示管线**：`GUI_DispString*` → 本地 VRAM `gui_vram[240*240]`(RGB565) → `OLED_CopyBuffer()`
  刷写 ST7789（与 LVGL 版相同的 SPI6 驱动）。
- **中文字体**：同 `003` 的 GBK 点阵方案（UNIGBK 双段 + GBKxx.FON），经 Unicode→GBK 取模。
- 资源：Debug 构型贴出 FLASH / RAM 占比（用于对比上一版），**双构零警告**。

## 十一、USB Host (TinyUSB, F4 U 盘 + 真正的 exFAT)

用 **TinyUSB 主机栈**把 U 盘（MSC→SCSI→FatFs）读出来，并支持 **真正的 exFAT**
（ChaN FatFs R0.15，`FF_FS_EXFAT=1`，非 FAT32 伪装）。以下为 F4 + FreeRTOS + SDRAM 平台验证结论：

- **USB 初始化必须在 `vTaskStartScheduler()` 之后**：`tusb_init()` 使能 OTG FS 中断，
  其 ISR 调用 FreeRTOS `xQueueSendToBackFromISR` 等 FromISR API —— 调度器未启动时非法，
  会把系统跑飞。故 `tusb_init()` 放 `usbh_host_task` 任务体内（该任务创建于调度器启动后）。
- **SDRAM / FreeRTOS 堆必须先于任何 RTOS 对象**：U 盘文件系统对象、LVGL draw buffer
  都落在外部 SDRAM，初始化顺序错会写未初始化内存 → heap 下溢断言。
- **FatFs 并发死锁**：两任务并发访问同一 U 盘（一个遍历 dump、一个挂载读字模），底层
  `disk_read/write` 用单个全局 busy 标志 + 自旋等完成回调 → 并发丢唤醒死锁。修复：用
  FreeRTOS 互斥量串行化所有 FatFs 入口（`fs_lock()/fs_unlock()`）。
- **exFAT 真实性**：`f_mkfs(FM_EXFAT, ...)` + 解析原始卷，验证 VBR 引导签名、簇堆对齐、
  分配单元大小（PC 端 gcc 编译的 harness 可给出 pass/fail 计数）。
- **GT911/GT9147 触摸中断风暴**：见 §四 与 `references/lan8720a-rmii.md`。

## 十一、USART1 非阻塞日志输出（环形缓冲 + TXE 中断）

裸 `printf` → `HAL_UART_Transmit` 会**阻塞调用线程直到整行发完**，在高速/中断密集场景拖累实时性。

> 本节只是速查摘要。日志系统的完整规范（三层架构、CMake 开关、裸机 TX 中断环形缓冲、
> RTOS 双 TX 路径 + 懒互斥量、串口不可用时的 SWD 取证、移植步骤与回归清单）见独立 skill
> **`stm32-logging-print-log`** —— **两者冲突时以该 skill 为准**。

三个最容易搞错的点：
- **中断源选 TXE，不要选 TC**：`TXE` = 发送数据寄存器空（可写下一字节），是逐字节 drain 环形缓冲的
  正确中断源；`TC` = 整帧移出、线路空闲，只在 RS485 方向切换等"线路空闲"场景用。
- **临界区 = 关闭 `UART_IT_TXE`**（裸机）或再加 `taskENTER_CRITICAL()`（RTOS）：
  `uart_write()` 先在临界区内改 `w/r/n` 索引，改完再开中断，使 ISR 与写者不可能同时竞争索引。
- **ISR 内禁止调 `PRINT_LOG`**（内部可能取互斥量），中断上下文一律直接 `uart_write()`。

## 十二、本 skill 的参考文件

按需读，不必一次全看：

- `references/lan8720a-rmii.md` — LAN8720A (RMII) 网络配置 + I2C 总线锁死恢复 +
  SDRAM/FMC 初始化顺序 + F429 LCD 8080 总线 + GT911 触摸（含中断风暴三层防护）
- `references/uart-physical-bridge.md` — UART 物理层与 CDC↔UART 透明桥踩坑
  （环形缓冲二义性 / 7bit+校验位污染 / RTS-CTS 成对配置 / Windows 不转发 RTS / D-Cache 与 DMA）
- `references/h7-flash-bootloader.md` — H7 内部 Flash 升级引擎六条铁律
  （DTCM 不可执行 / 双 Bank 取指 / RWW 语义 / HAL 原语 / 两阶段擦除 / 跳转 App 序列）
