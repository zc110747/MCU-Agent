# RA8D1 Vision Board 硬件分析（Phase 0）

> 证据来源标注：`[clock]` = 官方工程 `ra_gen/bsp_clock_cfg.h`；`[pin]` = `ra_gen/pin_data.c`；
> `[ld]` = `memory_regions.ld` / `script/fsp.ld`；`[cfg]` = `rtconfig.h` / Kconfig；`[yaml]` = 官方 BSP 描述文件；
> `[src]` = 官方工程源码。未确认项一律标注 **待确认**，不允许猜测。

## 1. MCU（R7FA8D1BH）

| 项目 | 值 | 来源 |
|---|---|---|
| 型号 | R7FA8D1BH（Vision Board） | [yaml] |
| 内核 | Arm Cortex-M85（Armv8.1-M，含 Helium MVE、I/D-Cache、MPU） | [yaml] + CMSIS 头 `R7FA8D1BH.h` |
| 主频 | 480 MHz | [clock] |
| Code Flash | 2 MB @ 0x02000000 | [ld] `FLASH_LENGTH=0x1F8000` |
| Data Flash | @ 0x27000000（BSP 保留区 0x3000） | [ld] |
| SRAM | 1 MB @ 0x22000000（BSP 实用 896 KB = 0xE0000） | [ld] + [src] board.h |
| ITCM | 64 KB @ 0x00000000 | [ld] |
| DTCM | 64 KB @ 0x20000000 | [ld] |
| QSPI Flash | 8 Mb（1 MB）@ 0x60000000 | [yaml] |
| SDRAM | 32 MB @ 0x68000000，16-bit 总线 | [cfg] `BSP_USING_SDRAM_SIZE=0x2000000` |
| Debug | SWD（板上 DAP-LINK，本项目改用 ART-Link CMSIS-DAP） | [yaml] |

> ⚠️ 冲突记录：[yaml] 写 "32Mb-SDRAM"（=4MB），但 [cfg] Kconfig/rtconfig 默认
> `BSP_USING_SDRAM_SIZE = 0x2000000`（=32MB），[ld] SDRAM 区域上限 0x8000000。
> 以 **BSP 代码 32MB 为准**，Phase 1 SDRAM 初始化实测作最终确认。

## 2. 时钟树（来源：rgb_4.3inch 工程 `bsp_clock_cfg.h`，[clock]）

- XTAL = **20 MHz**（注意：[yaml] 的 source_freq=8MHz 是 Studio 模板值，以生成代码为准）
- PLL1 = XTAL ×48 = 960 MHz
  - CPU = PLL1P /2 = **480 MHz**（ICLK /2 = 240，PCLKA /4 = 120，PCLKB /8 = 60，
    PCLKC /8 = 60，PCLKD /4 = 120，PCLKE /2 = 240，BCLK /4 = 120，FCLK /8 = 60）
  - SCICLK = PLL1P /4 = 120 MHz（UART 波特率源）
  - UCK = PLL1Q /5 = 48 MHz（USB FS）
- PLL2 = XTAL ×48 = 960 MHz
  - PLL2R /2 = **240 MHz → LCDCLK**（GLCDC 像素时钟源）
  - PLL2P/Q = 480 MHz（DRW 2D 引擎等）

## 3. 板载外设

| 外设 | 说明 | 来源 |
|---|---|---|
| LED | P102，GPIO 输出 | [src] blink_led `hal_entry.c` |
| Debug UART | UART9（SCI-B），115200 8N1，引脚 P208/P209 | [src] blink_led `hal_data.c` + `pin_data.c` |
| User Button | **待确认**（blink_led 工程未配置，需查原理图） | — |
| LCD RGB 4.3" | GLCDC，800×480，RGB565（16bpp），双缓冲 | [src] `common_data.h` |
| LCD MIPI-DSI | 2.0"/7.0"（GLCDC DSI 模式，r_mipi_dsi + r_mipi_phy） | [src] camera 工程 ra/fsp |
| Camera | 8-bit 并口 DVP（CEU）+ SCCB，支持 OV2640/OV5640/OV7670/OV7725/GC0328/GC2145/HM01B0 等 | [src] camera 工程 sensors/ |
| Touch | **CST812T**（I2C，本板 MIPI 2.0" 面板），从机 0x15，SCL=P408/SDA=P409（SCI3）、INT=P010、RST=P000 | [src] mipi 2.0" 工程 `cst812t.h` + 原理图 p3/p9 + SWD 实测 |
| SDRAM 驱动 | BSP 自带 `drv_sdram.c`：CL=3、TRAS=6、TRCD=3、TRP=3、TWR=2、TRFC=8，初始化后注册 memheap | [src] |
| USB | FS（P40x 引脚组）+ HS；后续 TinyUSB 可用 | [pin] |

## 4. GLCDC/FrameBuffer 关键参数（rgb_4.3inch，[src] `common_data.h`）

- Layer0：`DISPLAY_HSIZE_INPUT0=800`，`DISPLAY_VSIZE_INPUT0=480`
- 像素格式：RGB565（16bpp），stride = `((800×16+0x1FF)>>9)<<6` = **1664 字节**
- Framebuffer：`fb_background[2][1664×480]`，64 字节对齐，位于 `.sdram` 段（0x68000000）
  - 单缓冲 ≈ 798,720 B，双缓冲 ≈ 1.6 MB → 必须放 SDRAM
- Backlight = P1011，LCD_RST = P1104（来源：`libraries/HAL_Drivers/config/ra8/lcd_config.h`）

## 5. Cache / DMA 关注点（Phase 2 落实）

- Cortex-M85 带 I/D-Cache；GLCDC 为总线主设备直接读 SDRAM 中的 framebuffer。
- BSP 链接脚本（`script/fsp.ld`）预留两个 NOLOAD 段：
  - `.nocache`（片内 RAM）、`.nocache_sdram`（SDRAM 内）——供 DMA 缓冲区免一致性问题
- 可选方案：① framebuffer 放 MPU 非缓存区；② CPU 写后 cache clean。Phase 2 依据实测选择，**不许无解释地随手 clean/invalidate**。

## 6. 官方资料优先级

官方原理图 > RA8D1 Datasheet/User Manual > 官方 BSP 代码 > 其他资料。
文档位置：`E:/cnb/git/sdk-bsp-ra8d1-vision-board-master/documents/`（仅作只读参考，不参与构建）。
