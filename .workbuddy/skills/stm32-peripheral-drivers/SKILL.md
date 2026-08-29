---
name: stm32-peripheral-drivers
description: STM32 外设驱动速查表与实测踩坑：STM32H743 / STM32F429 引脚映射、OV5640(DCMI) 多缓冲采集、ST7789(SPI6) OLED 显示、SD 卡 FatFs + GBK 中文点阵字库、QSPI(W25Q64) Flash、USB OTG_FS 的 VDD33USB 供电坑、LAN8720A(RMII) 网络、I2C 总线锁死恢复。适用于"查 STM32 引脚""移植摄像头/OLED/SD 卡驱动""GBK 字库渲染""USB 设备枚举不上""I2C 死锁"。触发词：STM32H7 引脚、STM32F4 引脚、OV5640、DCMI、ST7789、SPI6、FatFs 字库、GBK 字库、QSPI、W25Q64、VDD33USB、USB 枚举不上、LAN8720、RMII、I2C 锁死、PCF8574、SDRAM、FMC、OV5640 DCMI 基地址 0x48020000、F429 LCD 8080 NT35510 扫描方向交换、GT911 触摸中断风暴、SDIO 4字节对齐、TJpgDec swap 涂抹、MPU9250 磁力计可选、EXFAT 字库挂载、emWin STemWin、USB Host TinyUSB、exFAT U盘、PRINT_LOG 日志开关、GT911 中断风暴三层防护。
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
