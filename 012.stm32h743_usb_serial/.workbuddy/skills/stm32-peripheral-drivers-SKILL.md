---
name: stm32-peripheral-drivers
description: STM32 外设驱动速查表与实测踩坑：STM32H743 / STM32F429 引脚映射、OV5640(DCMI) 多缓冲采集、ST7789(SPI6) OLED 显示、SD 卡 FatFs + GBK 中文点阵字库、QSPI(W25Q64) Flash、USB OTG_FS 的 VDD33USB 供电坑、LAN8720A(RMII) 网络、I2C 总线锁死恢复、UART+DMA 环形接收与 RTS/CTS 流控（Circular DMA 用 NDTR 反推写指针、7 数据位+校验时校验位污染数据字节、RTS 配 AF 模式导致引脚悬空、DTCM 不可被 DMA 访问、D-Cache 无 MPU 维护）、TinyUSB CDC 串口桥背压与 EP0 vendor 管理通道、PC 串口压测工具 Windows 踩坑。适用于"查 STM32 引脚""移植摄像头/OLED/SD 卡驱动""GBK 字库渲染""USB 设备枚举不上""I2C 死锁""UART DMA 丢包""RTS CTS 流控不生效""7 数据位校验位""USB CDC 转串口"。触发词：STM32H7 引脚、STM32F4 引脚、OV5640、DCMI、ST7789、SPI6、FatFs 字库、GBK 字库、QSPI、W25Q64、VDD33USB、USB 枚举不上、LAN8720、RMII、I2C 锁死、PCF8574、SDRAM、FMC、OV5640 DCMI 基地址 0x48020000、F429 LCD 8080 NT35510 扫描方向交换、GT911 触摸中断风暴、SDIO 4字节对齐、TJpgDec swap 涂抹、MPU9250 磁力计可选、EXFAT 字库挂载、emWin STemWin、USB Host TinyUSB、exFAT U盘、PRINT_LOG 日志开关、GT911 中断风暴三层防护、UART4 DMA 环形缓冲、Circular DMA NDTR、HT TC IDLE、RS485/RTS 失效、HAL_GPIO_WritePin 复用引脚无效、DTCM DMA、axim、TinyUSB CDC、SET_LINE_CODING、串口桥压测、pyserial read 虚假延迟、in_waiting 为 0、串口往返延迟测量、定长定间隔发送 paced、延迟模型 0.31ms、Windows usbser RTS 不转发、DTR 只转发、UART4 无 DTR 引脚、流控固件自管理。
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

### 8.4 F429 LCD 8080 总线（正点原子 800×480 屏，NT35510/ILI9806E）
- 控制器 NT35510（`0x8000`）/ ILI9806E 回退；FMC Bank1 NE1 8080 16-bit，`RS=A18`
  （`LCD_BASE = 0x60000000 | 0x0007FFFE`）。
- **`lcd_scan_dir` 的宽高交换逻辑是正点原子原版、正确，切勿改反/删除**：`DFT_SCAN_DIR=L2R_U2D`(MV=0)
  下因 `lcd_width(800) > lcd_height(480)` 触发交换 → 有效 GRAM 窗口 **480×800**（即 NT35510 模块铺满
  物理 800×480 屏所需的窗口）。屏幕尺寸只由 `LCD_WIDTH/LCD_HEIGHT` 决定，不要动交换逻辑。
- **LVGL 画布 = GRAM 窗口 = 480×800**（不是 800×480）；UI 布局从 `lv_disp_get_hor_res/ver_res()`
  自适应取，不要硬编码 800×480，否则渲染错位。
- **LCD 地址窗口必须用 MIPI-DCS 时序**：命令写一次 + 跟 4 数据字节；不可把 `0x2A/0x2B/0x2C`
  当连续寄存器拆写，否则渲染带写错乱 GRAM → 文字重叠。

### 8.5 电容触摸 GT911/GT9147（软件位绑定 I2C）
- 芯片**只有 I2C 模式（无 SPI）**，走软件位绑定 I2C：排针 `T_SCK(PH6)=CT_SCL`、`T_MOSI(PI3)=CT_SDA`、
  `T_CS(PI8)=CT_RST`、`T_PEN(PH7)=CT_INT`。板上实贴 GT911（`product ID="911"`，addr `0x14`，自报 480×800 与画布一致）。
- **184B 配置块是 9147 专用，绝不能给 GT911 上传**；GT911 用恒等映射即可。
- **`T_PEN(PH7)` 极易产生中断风暴**：浮空输入 + 紧邻 PH6(位绑定 SCL 165kHz) 串扰，实测 ~46925 次/秒；
  必须上拉，且 ISR 内立即屏蔽 line 7、任务侧消抖后重新武装（噪声中断正解是「ISR 内屏蔽 + 任务侧延时重新武装」，
  不是在 ISR 里做软件滤波）。给所有外部中断加「1s 速率看门狗」把风暴变成数字，最划算。
- **GT9xx 中断后必须轮询到抬手**：INT 行为因模组而异（单次/持续脉冲），中断只当唤醒，任务随后 15ms 轮询直到连续 3 次无触点。
- 无需手指即可验证 EXTI 链路：OpenOCD `halt` → `mww 0x40013C10 0x80`（`EXTI->SWIER`）→ `resume`，产生与引脚边沿等价的中断。

### 8.5.1 GT911/GT9147 中断风暴三层防护（来自 102 真机教训）

**症状**：PH7 浮空 + 紧邻 PH6（位绑定 SCL 165 kHz）串扰 → 实测 **~46925 次/秒** 中断；
触摸任务优先级高于传感器任务 → 轮询式 `HAL_I2C_Mem_Read` 被抢占 → HAL 超时 → 从机拉住 SDA
→ I2C 总线 BUSY 锁死（之后每次都失败）。**三层防护缺一不可**：

| 层 | 措施 |
|---|---|
| 源头 | PH7 改 `GPIO_PULLUP`（仅地址锁存一瞬 NOPULL）；位绑定 I2C 事务期间屏蔽 EXTI line 7 |
| 隔离 | 传感器读取用 `vTaskSuspendAll()/xTaskResumeAll()` 包成原子操作（中断仍开），传输不被抢占 |
| 容错 | I2C 超时 10ms→50ms；失败先 `BSP_I2C_Recover()` 再立即重试一次；ISR 内立即屏蔽 line 7、任务侧消抖后重新武装（速率 ~47kHz→~20Hz）；触摸轮询上限 `TOUCH_MAX_POLLS` 兜底；IRQ 速率看门狗超 1000/s 打 WARNING |

验证：`tools/verify_serial/verify_sensors.py` 经 SWD 直读 `s_data`，**7/7 PASS**
（`errors=0`、`|a|=1.00g`）；`verify_touch_irq.py` 经 `EXTI_SWIER` 软注入 line 7，**7/7 PASS**。

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
  （同芯片已验证的 `drv_flash.c` / `upload_frame.c`，可作为外部参考，非本仓 skill）。

## 十、emWin (STemWin) GUI 栈（H7 + ST7789，无 OS）

STemWin（Segger emWin 的 ST 版）是 LVGL 之外的另一套 GUI 方案，在 `003` 的 LVGL 版基础上
1:1 重做了整套 H7 ST7789 OLED 信息面板（见 `011.stm32h743_freertos_emwin`）。

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
- 资源（Debug）：FLASH 133532B/2MB(6.37%)、RAM 271808B/512KB(51.84%)，双构零警告。

## 十一、USB Host (TinyUSB, F4 U 盘 + 真正的 exFAT)

`102.stm32f429_tinyusb_ui` 用 **TinyUSB 主机栈**把 U 盘（MSC→SCSI→FatFs）读出来，
并支持 **真正的 exFAT**（ChaN FatFs R0.15，`FF_FS_EXFAT=1`，非 FAT32 伪装）。

- **USB 初始化必须在 `vTaskStartScheduler()` 之后**：`tusb_init()` 使能 OTG FS 中断，
  其 ISR 调用 FreeRTOS `xQueueSendToBackFromISR` 等 FromISR API —— 调度器未启动时非法，
  会把系统跑飞。故 `tusb_init()` 放 `usbh_host_task` 任务体内（该任务创建于调度器启动后）。
- **SDRAM / FreeRTOS 堆必须先于任何 RTOS 对象**（同 §8.3）：U 盘文件系统对象、LVGL draw buffer
  都落在外部 SDRAM，初始化顺序错会写未初始化内存 → heap 下溢断言。
- **FatFs 并发死锁**：两任务并发访问同一 U 盘（一个遍历 dump、一个挂载读字模），底层
  `disk_read/write` 用单个全局 busy 标志 + 自旋等完成回调 → 并发丢唤醒死锁。修复：用
  FreeRTOS 互斥量串行化所有 FatFs 入口（`fs_lock()/fs_unlock()`）。
- **exFAT 真实性**：`f_mkfs(FM_EXFAT, ...)` + 解析原始卷，验证 VBR 引导签名、簇堆 128KB 对齐、
  分配单元 128KB（见 `102/verify_exfat/harness.c`，PC 端 gcc 编译，`12 passed`）。
- **GT911/GT9147 触摸中断风暴**：见 §8.5.1（ISR 内屏蔽 line + 任务侧重新武装 + 速率看门狗）。

## 十二、UART DMA 桥 + RTS/CTS 流控（H7 UART4 + TinyUSB CDC 真机实测坑）

工程参考：`012.stm32h743_usb_serial`（H743ZIT6，HSE 25MHz，SYSCLK 400MHz，APB1 100MHz 供 UART4）。
引脚：TX=PA0(AF8) / RX=PA1(AF8) / CTS=PB0 / RTS=PB14。PA0 与 PA1 短接做自回环压测。

### 12.1 Circular DMA RX：用 NDTR 反推写指针，别依赖 HT/TC/IDLE 搬运

- DMA1_Stream0 circular，2KB。**HT/TC/IDLE 只当"有数据了"的提示，不用来搬运数据**。
- 每次排空直接读 NDTR 反推 DMA 写指针，排空逻辑天然幂等：

  ```c
  wr = (SIZE - (NDTR & (SIZE-1))) & (SIZE-1);
  n  = (wr - head) & (SIZE-1);        /* 最多两段（回绕），每段 Invalidate 后入 ring */
  s_rx_pos = (head + n) & (SIZE-1);
  ```

- 好处：三个事件同时发生 / 事件丢失 / 重复触发都不重不漏；DMA 永不停，缓冲满即回绕。
- 另加 2ms `force` 兜底巡检防中断丢失。
- 验收判据：固件内部 `uart_stats_t`（SWD 直读 `g_uart_stats`）里 `idle / ht / tc` **三者都必须 > 0**，证明三条路径真被走到
  （实测一次 mixed 压测 `idle=1522 ht=159 tc=156`）。

### 12.2 ⚠️ 字长含校验位：7 数据位 + 校验会把校验位读进数据字节

STM32 的 `WordLength` 是**含校验位的总帧长**（M[1:0]：00=8、01=9、10=7）：

| CDC 参数 | 总位 | WordLength | RDR 里的数据位 | DMA 按字节读 |
|---|---|---|---|---|
| 8N | 8 | `8B` (M=00) | bit7:0 | 正确 |
| 8E / 8O | 9 | `9B` (M=01) | bit7:0，校验在 **bit8（字节外）** | 正确 |
| 7N | 7 | `7B` (M=10) | bit6:0 | 正确 |
| **7E / 7O** | **8** | **`8B` (M=00)** | bit6:0，**校验在 bit7** | **污染数据！** |

所以 7 数据位 + 校验时，字节宽度 DMA 会把校验位当数据读进来。
**修法**：按数据位算掩码，排空时就地掩蔽（8 数据位时跳过，不影响主路径）：

```c
s_rx_data_mask = (data_bits >= 8) ? 0xFF : (uint8_t)((1u << data_bits) - 1u);
/* 排空循环内，Invalidate 之后、入 ring 之前 */
if (s_rx_data_mask != 0xFFu)
    for (uint32_t i = 0; i < span; i++) s_rx_dma[off + i] &= s_rx_data_mask;
```
就地改 DMA 缓冲是安全的：该 span 的字节已被 DMA 写完且已消费，DMA 只会覆盖不会回读。

> 排查现象：7N1 通过、7E1/7O1 全挂，8E1/8O1 却正常。
> 另注：7 数据位模式**无法承载任意二进制**（MSB 在线路上不存在），只能用可打印 ASCII 验证。

### 12.3 ⚠️⚠️ RTS/CTS 必须配成普通 GPIO，配 AF 会让流控形同虚设

- 若 `huart.Init.HwFlowCtl` 保持 `UART_HWCONTROL_NONE`（软件流控方案），
  **USART 根本不驱动 RTS**；而引脚一旦配成 `GPIO_MODE_AF_PP`，
  `HAL_GPIO_WritePin()`（写 BSRR）**对处于复用功能的引脚无效**。
- 结果：RTS 引脚实际高阻悬空，软件变量却显示"已断言"。若 RTS 与 CTS 短接，
  CTS 被内部上拉拉高 → 永远"未就绪" → 门控模式一帧不通。
- **修法**：RTS 配 `GPIO_MODE_OUTPUT_PP` 由软件按接收环余量驱动；
  CTS 配 `GPIO_MODE_INPUT` + `GPIO_PULLUP`（悬空读作"未就绪"，安全侧失效）。

  ```c
  g.Pin = RTS_PIN; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Alternate = 0;
  HAL_GPIO_WritePin(RTS_PORT, RTS_PIN, GPIO_PIN_RESET);   /* 上电即 ready */
  ```
- 不用硬件 RTS 的理由：硬件 RTS 只跟踪 1 字节 RDR 标志，对 16KB ring 毫无意义。

> 排查现象：`AT+STATS` 里 `RTSO=1` 但 `CTS=0`，无论哪种流控模式。
> 修复前四模式 `CTS=0` 全部不通，修复后 `CTS=1` 全部跑通。

### 12.4 环形缓冲 head==tail 二义性（经典但致命）

`head == tail` 同时表示"空"和"满"。若 `rb_free()` 用 `cap - used`，
满会被误判为空 → 生产者覆盖未消费数据（实测首轮压测 lost 达 3.6e10）。
**修法**：故意保留 1 字节，可用容量 = `cap - 1`，`rb_free()` 与 `rb_write()` 都按 `cap-1` 算。

### 12.5 DMA 缓冲不能放 DTCM；无 MPU 时的 cache 维护

- **DTCM (0x20000000) 不可被 DMA1/DMA2 访问** → DMA 缓冲必须放 AXI SRAM `0x24000000`。
- 链接脚本加 `NOLOAD` 的 `.dma_buf` 段（`ALIGN(32)`，尺寸 32B 整数倍）：
  NOLOAD 保证 C 启动不触碰它 → 首次 DMA 前无脏行 → 纯 Invalidate 安全。
- 维护用 `SCB_CleanDCache_by_Addr` / `SCB_InvalidateDCache_by_Addr`，起止地址按 32B 外扩。
- **构建期校验段放置**（`cmake/check_dma_section.cmake` 用 `objdump -h` 解析 VMA/size，
  校验非空、不在 DTCM、VMA 32B 对齐、size 32B 整数倍，否则 `FATAL_ERROR`）。
  这类"放错内存就静默出错"的约束，务必做成构建期硬失败。

### 12.6 USB CDC 背压会饿死带内命令通道（设计约束）

- 流控门住 TX 且 TX ring 灌满后，设备对主机 NAK → **`+++` 逃逸序列也发不进去**，
  AT 命令全部失效。这是背压的正确行为（不能为收命令而丢数据），不是 bug。
- **结论：管理通道必须与数据通路解耦**。本项目用 USB **EP0 vendor 控制请求**
  （`tud_vendor_control_xfer_cb`）作为永不被背压阻塞的第二通道。
- 测试侧应对：查询统计前先 `drain_quiet()` 排空接收侧，让设备自己解锁。

### 12.7 PC 端串口测试工具踩坑（Windows）

- **COM 端口独占**：同一进程内不可开两次。配置用的 handle 必须先关，再开压测用的 handle。
- **关端口后立刻重开会被拒**（`PermissionError`）：封装带重试的 `open_serial()`（20×0.15s）。
- **必须先 join RX 线程再 close**：`run()` 里写超时异常会跳过 `stop()`，RX 线程变僵尸
  仍占着句柄 → 后续 `open` 全挂。`close()` 里应 `stop_evt.set() → join() → ser.close()`。
- **主机侧必须限在途字节窗口**（默认 2048）：USB 全速远快于 UART，不限流会把主机驱动缓冲
  打满，产生"固件丢包"的假象。窗口等待循环**必须同时检查总时长**，否则设备完全不回数据时
  会卡死在内层循环。
- **测平均延迟前先想清楚**：延迟主要由窗口大小/波特率决定，不是固件固有延迟。
  空载往返延迟另有大坑，见 **12.9**。

### 12.8 自回环压测的验收判据（可直接复用）

- 帧格式 `A5 5A | len(u16 BE) | seq(u32 LE) | payload | crc32(u32 LE)`，
  载荷用 `payload_for(seq,n)` 确定性生成 → 无需缓存已发内容即可校验。
- 断言：`lost=0 / duplicate=0 / crc_error=0 / payload_error=0 / resync=0`。
- 固件侧断言：`rx_drop=0 tx_drop=0 ore=0 fe=0 pe=0 ne=0`，且 `reconf_fail=0`。
- 流控自节流对比（把 16KB 接收环顶到 16383/16384）：
  RTS_ONLY（无人理会 RTS）→ `rx_drop≈7e5`（每次运行不同，703008 / 703280 都测到过）；
  RTS_CTS（短接自握手）→ **`rx_drop=0`**。
  **判据是定性的**：`rts_off>0`、`cts_wait>0`（≈1.8e6~2.5e6）、RTS_CTS 下 `rx_drop==0`。

### 12.9 ⚠️⚠️ PC 侧 `ser.read(4096)` 制造 30 ms 虚假延迟（最会骗人的一条）

**症状**：空载 1 字节小包往返延迟 34 ms，看起来像 USB 轮询 + 固件排队被放大 30 倍，
于是疯狂去优化固件 —— 实际上固件一点问题没有。

**根因**：

```python
data = ser.read(ser.in_waiting or 4096)     # 错
```

`in_waiting == 0` 时请求 **4096 字节**。Windows/pySerial 的读语义是
**"尽量凑满请求字节数，凑不满就等到读超时"**，于是每次无数据时空等整整一个 timeout，
把 0.2 ms 就该返回的字节拖到 30 ms 才交出来。

**修复**：

```python
n = ser.in_waiting
data = ser.read(n if n else 1)              # 对
```

**定位方法（重要，可复用）**：写个对照脚本，对比两条路径——

| 路径 | 修复前 avg | 修复后 avg |
|---|---|---|
| 软件回环 `AT+LOOP=1`（**完全绕过 UART/DMA**） | 31.91 ms | 0.22 ms |
| 经 UART4 真实硬件回环 | 34.24 ms | **1.43 ms** |

软件回环根本不碰 UART 却同样 31.91 ms → 30 ms 与固件无关，直接锁定 PC 侧。
（工具已固化为 `tools/latency_probe.py`。）

> **杀伤力在于它只影响轻负载**：灌满缓冲时 `in_waiting` 恒非零，症状完全消失；
> 只有测空载往返（`paced`、小包延迟）才暴露。所以吞吐压测全绿不代表测量代码是对的。
>
> 由此得到的固件固有延迟数字：**软件回环 0.22 ms**（USB CDC + ringbuf + 主循环，
> 完全不碰 UART），可作为任何 STM32 USB↔串口桥的基线参考。

### 12.10 定长 / 定间隔 / 定量发送测试（`paced`）与延迟模型

测试意图：**每帧发完等回环返回再发下一帧**，不设在途窗口，测真实往返延迟而非吞吐。

三档实测（115200 8N1，未开流控，PA0↔PA1 短接，全部 `byte-exact yes`、错误全 0）：

| 载荷 N | 帧数 | 线路字节 | 延迟 min / avg / max |
|---|---|---|---|
| 1 B × 100 帧 @10 ms | 100/100 | 1300 | 1.372 / **1.437** / 1.583 ms |
| 10 B × 100 帧 @20 ms | 100/100 | 2200 | 2.143 / **2.232** / 2.574 ms |
| 500 B × 20 帧 @500 ms | 20/20 | 10240 | 44.692 / **44.768** / 44.854 ms |

**延迟模型**（三档残差均 < 15 μs，可直接用来预判任意长度）：

```
latency ≈ 0.31 ms 固定开销（USB 轮询 + 固件搬运 + PC 读）
        + (12 + N) × 10 bit / 115200          # 12 = 同步字2+长度2+序号4+CRC4
```

| N | 线路帧 | 纯传输 | +固定 | 实测 avg | 残差 |
|---|---|---|---|---|---|
| 1 | 13 B | 1.128 ms | 1.437 ms | 1.437 ms | 0.000 ms |
| 10 | 22 B | 1.910 ms | 2.219 ms | 2.232 ms | +0.013 ms |
| 500 | 512 B | 44.444 ms | 44.753 ms | 44.768 ms | +0.014 ms |

**判读要点**：残差几乎为 0 → 延迟**完全由波特率决定**，固件/USB 没有引入任何与长度相关的
额外排队。若某档残差突然变大（比如 500 B 比预测多出几 ms），才是固件存在分片/排队问题的信号。

> 注意「1 B 载荷」在线路上是 **13 B**，测的是 13 B 的往返时间——别拿它和"1 字节传输时间"比。
> 另：`间隔`是帧间隔不是速率上限，实测时长必然大于 `帧数 × 间隔`。

### 12.11 ⚠️ Windows `usbser.sys` 不转发 RTS（仅 DTR）+ UART4 无 DTR 引脚 → 流控必须固件自管理

**已实验证实**（固件在 `tud_cdc_line_state_cb` 里对每次回调打标记，PC 侧用 `EscapeCommFunction`
切 RTS/DTR）：Windows 自带 `usbser.sys` 在端口**打开后**只把 DTR 变化转发给设备
（`CLRDTR` / `SETDTR` 触发回调），**RTS 变化不转发**（`CLRRTS` / `SETRTS` 不触发回调）。

**推论**：
- 「由主机 RTS/DTR 切换来开关流控」在 Windows 上位机**不可行**——RTS 到不了固件。
- UART4 本身**没有 DTR/DSR 引脚**，DTR 只能「观测」、不能门控数据通路。
- 因此：**流控必须是固件自管理 + 连接门控的**——把 CTS/RTS 流控位与 USB 端口的「打开/关闭」绑定：
  - 主机 DTR 置位（端口打开）→ `HwFlowCtl = UART_HWCONTROL_CTS`（CTSE 使能，对端 CTS 硬件门控 TX），
    自己的 RTS（PB14）由软件按接收环余量 + 主机 RTS 驱动。
  - 主机 DTR 撤除（断开）→ `HwFlowCtl = UART_HWCONTROL_NONE`（CTSE 清除）且把 UART 还原为
    默认 115200/8N1/无校验，RTS 引脚置为无效（高）。
  - 实现上：`tud_cdc_line_state_cb` 只记录 DTR/RTS 到主循环变量；主循环调用一个
    `connection_service()` 比较「期望流控态」与「已应用态」，变化时才 `HAL_UART_Init` 重配。
    不要在该回调里直接 `HAL_UART_Init`（它运行在 `tud_task()` 主循环上下文，但重配应集中、去抖）。
  这天然覆盖单 RTS / 单 DTR / 双 RTS-DTR 三种主机情况，无需任何固件模式开关，也不依赖主机是否转发 RTS。

**后果 / 验证提示**：
- 回环流控脚本（PB0 短接 PB14）的「去断言 RTS → TX 门控」在 Windows 上**不会门控**
  （属主机驱动限制，非固件缺陷）；在 Linux/macOS 或会转发 RTS 的 CDC 驱动上才真正门控。
- 「仅关 DTR」测试在 Windows 上能跑通，正好印证 DTR 只观测、不门控。
- 透明性验证（`tools/transparency_probe.py`）：DTR 开/关均原样收发、`+++AT+...` 不被吞。
