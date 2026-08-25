---
name: stm32-peripheral-drivers
description: STM32 外设驱动速查表与实测踩坑：STM32H743 / STM32F429 引脚映射、OV5640(DCMI) 多缓冲采集、ST7789(SPI6) OLED 显示、SD 卡 FatFs + GBK 中文点阵字库、QSPI(W25Q64) Flash、USB OTG_FS 的 VDD33USB 供电坑、LAN8720A(RMII) 网络、I2C 总线锁死恢复。适用于"查 STM32 引脚""移植摄像头/OLED/SD 卡驱动""GBK 字库渲染""USB 设备枚举不上""I2C 死锁"。触发词：STM32H7 引脚、STM32F4 引脚、OV5640、DCMI、ST7789、SPI6、FatFs 字库、GBK 字库、QSPI、W25Q64、VDD33USB、USB 枚举不上、LAN8720、RMII、I2C 锁死、PCF8574、SDRAM、FMC。
agent_created: true
---

# STM32 外设驱动速查与实测踩坑

已验证的两套硬件：STM32H743ZIT6（鹿小班核心板，HSE 25MHz 无源晶振）与
STM32F429IGT6（HSE 25MHz）。所有引脚均来自真机原理图，可直接复用。

## 一、STM32H743ZIT6 引脚速查

| 功能 | 引脚 | 备注 |
|---|---|---|
| 调试串口 USART1 | PA9(TX)/PA10(RX) | 115200 8N1，ST-Link 虚拟串口 COM19 |
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
| 调试串口 USART1 | PA9(TX)/PA10(RX) | COM3，115200 8N1 |
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

### 5.5 中文字符编码坑（008 项目真机验证）
- FatFs `FF_CODE_PAGE` 必须 `936`（GBK），**绝不可改 437**（否则中文长文件名/字库路径乱码）。
- 文本渲染前用 `utf8_is_valid()` 识别"无 BOM 的合法 UTF-8"，避免把 GBK 字节误当 UTF-8 转码
  （GBK 双字节高字节 0x81–0xFE 常被误判 UTF-8 续字节 → 乱码）。
- 有 BOM 的 UTF-8（`EF BB BF`）走快路径不转码。

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

### 7.1 CMSIS-DAP 探针接线（007 项目）
自研 CMSIS-DAP v1 探针（H743 鲁小板 + TinyUSB HID）访问目标时，SWD 目标线接到：
SWCLK=PA0 / SWDIO=PA1 / nRESET=PA2 / SWO=PA3 / 闲置=PA5 / 目标供电检测=PA7。
**三处线勿混**：烧写线 / USB 上行线 / SWD 目标线。SWD 时序延时不可用 Keil `__asm` `_DELAY`，
改用 DWT 周期计数（`DELAY_SLOW_CYCLES=1` 级）做精确延时。

## 八、LAN8720A (RMII) 网络 + I2C 锁死恢复（F4）

### 8.1 ETH 配置
- RMII，PHY addr 0；REF_CLK 来自 PA1（需 PHY 提供 50MHz 时钟或 H7/F4 输出）。
- 复位经 PCF8574T P7（高有效，经三极管反相 → 写 1 时 ETH_RESET=0 释放，写 0 时复位）。
- **ETH RX 零拷贝缓冲必须留内部 SRAM**：ETH DMA 写 SDRAM 分片突发丢包
  （`ping -l 1473` 起不稳定）；memp 池也在 SRAM。发送侧放 SDRAM 无碍。

### 8.2 I2C 总线锁死恢复（运行期必装）
从设备把 SDA 拉低锁死（噪声/复位中途打断事务）→ HAL 读永久超时 → 数据冻结为 0。
`BSP_I2C_Recover()`：检测 SDA 低/BUSY/错误标志 → SCL 配 GPIO 推挽翻转 ≥9 次释放从设备
→ 发 STOP → `HAL_I2C_DeInit+Init` → 还原 AF_OD。纯 `__NOP()` 延时，**调度器启动前也安全**，
须在 `BSP_ETH_PHY_Reset()` 前调用（防启动期死等）。

### 8.3 SDRAM (FMC) 初始化必须最先
FreeRTOS heap / LwIP 池 / mbedTLS 池都在 SDRAM，任何 `xTaskCreate` 在 SDRAM 前会写
未初始化内存 → `heap_4.c:269` 下溢断言。FMC 配置 → SDRAM 初始化序列 → 刷新率 → 内存自测，
全部早于一切 RTOS 对象。

## 九、STM32H7 内部 Flash 升级引擎（Bootloader 实战坑）

H7 内部 Flash 是**双 Bank（16×128KB）**，做 Bootloader 升级时，下面每一条都能让"擦写函数一跑就死"：

### 9.1 DTCM 不可执行（最高频致命坑）
Cortex-M7 的 **I-Code 总线无法从 DTCM(0x20000000) 取指**。把"从 RAM 执行"的擦写引擎放进 DTCM → 一调用就 BusFault → `Default_Handler`(Infinite_Loop)。
**引擎必须放 AXI SRAM(0x24000000)** 或 SRAM1-4（可执行且非 bank1）。MPU 的 XN=0 管不到 TCM 硬件约束 —— 改 MPU 没用。

### 9.2 双 Bank 擦写取指
擦/写 bank1 时 CPU 不能从 bank1 取指。擦写引擎须整体在独立可执行 RAM 内，且调用链（含所有 static helper，如 `addr_to_bank_sector`）都得带 RAM 段属性，不能残留 bank1 Flash 调用。

### 9.3 RWW 只 stall 不 fault
H7 的 read-while-write 会让总线停滞但**不 HardFault**。所以从 bank1 取指编程 bank1 不会进 `Infinite_Loop` —— 这正好解释为什么参考代码能直接调用位于 bank1 的 `HAL_FLASH_Program`。

### 9.4 手搓寄存器不可靠 → 用 HAL
自写 `FLASH->CR` 序列极易踩 PSIZE/时序细节。改用**经验证 HAL 原语**：
- 擦除：`HAL_FLASHEx_Erase()`（`VoltageRange = FLASH_VOLTAGE_RANGE_3`）
- 编程：`HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr, (uint32_t)src)`（一次写 256 位 = 8×32bit）
- **每次操作前清双 Bank 错误标志**：`__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_ALL_ERRORS_BANK2)`
- 用 `__disable_irq()`+`HAL_FLASH_Unlock()` 包住、`HAL_FLASH_Lock()`+`__enable_irq()` 收尾。

### 9.5 两阶段长度擦除
不要整片擦 1..14。先 `BFLASH_EraseApp(len)` 按 App 长度只擦实际占用（不含末扇区），末扇区由 `BFLASH_EraseAppLastSector(len)` 在**编程前即时擦**（避免整片先擦 + 中途掉电变砖）。

### 9.6 跳转 App 序列（缺一不可）
跳前必须：`HAL_DeInit` / 关全局中断 / 关 MPU / 关 I-D Cache / 设 MSP / 重定位 VTOR / `__enable_irq`（清 PRIMASK 残留）。
- **App 工程必须提供 `SysTick_Handler`** 且 `main()` 开头 `__enable_irq()`，否则 `HAL_Delay` 卡死（缺 handler → 链到 `Default_Handler` 死循环；bootloader 留 PRIMASK=1 未恢复 → 全局中断关死）。
- 验证链路见 `stm32-verification-acceptance` 第八、九节；黄金外部参考样本：`7.stm32h7_iap`
  （同芯片已验证的 `drv_flash.c` / `upload_frame.c`，外部路径
  `D:/user_project/coding_git/embeds/stm32h7_project/7.stm32h7_iap`，**非本仓 skill**）。
