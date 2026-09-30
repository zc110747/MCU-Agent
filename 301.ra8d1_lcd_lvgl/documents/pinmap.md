# 引脚映射表（Phase 0）

> 每一条都必须带来源。`待确认` 项在后续 Phase 从原理图 PDF / Datasheet 补齐，禁止猜测填充。

## 1. 已确认（来自官方 BSP 生成代码/源码）

| Signal | Pin | Function | Source |
|---|---|---|---|
| Debug UART TX | P208 | UART9 (SCI9) TXD，115200 8N1 | blink_led `ra_gen/pin_data.c`（IOPORT_PERIPHERAL_SCI1_3_5_7_9）+ `hal_data.c`（channel=9） |
| Debug UART RX | P209 | UART9 (SCI9) RXD | 同上 |
| LED | P102 | 板载 LED，GPIO 输出 | blink_led `src/hal_entry.c`（`LED_PIN = BSP_IO_PORT_01_PIN_02`） |
| Touch INT | P010 | GT9147 中断，输入 | rgb 工程 `src/hal_entry.c`（`INT_PIN "p010"`） |
| Touch RST | P000 | GT9147 复位，输出 | rgb 工程 `src/hal_entry.c`（`RST_PIN "p000"`） |
| Touch/SCCB I2C | P512（组） | IOPORT_PERIPHERAL_IIC | rgb 工程 `pin_data.c` |
| LCD Backlight | P1011 | PWM/GPIO 背光 | `libraries/HAL_Drivers/config/ra8/lcd_config.h` |
| LCD Reset | P1104 | 屏复位 | 同上 |
| LCD RGB 总线（组） | P3xx 多脚 | IOPORT_PERIPHERAL_BUS（高驱动），RGB 数据/DE/HSYNC/VSYNC/PCLK | rgb 工程 `pin_data.c`（逐脚功能表待 Phase 2 原理图确认） |
| LCD 时钟控制（组） | P207、P515、P60x | IOPORT_PERIPHERAL_LCD_GRAPHICS | rgb 工程 `pin_data.c` |
| Camera CEU（组） | P400、P401、P405、P406、P407 | IOPORT_PERIPHERAL_CEU | rgb 工程 `pin_data.c`（完整 8-bit 数据线映射待 Phase 3 原理图确认） |
| QSPI/OSPI Flash（组） | P100、P101、P103、P107 | IOPORT_PERIPHERAL_OSPI | rgb 工程 `pin_data.c` |
| SDRAM DBUS（组） | P112~P115、P3xx、P6xx（BUS 组内） | IOPORT_PERIPHERAL_BUS（高驱动） | rgb 工程 `pin_data.c`（SDRAM 专用脚逐脚确认待 Phase 1） |
| USB FS（组） | P40x | IOPORT_PERIPHERAL_USB_FS | rgb 工程 `pin_data.c` |

> ⚠️ 冲突记录：[yaml] 称 debug 串口为 "PA9/PA10"。RA8D1 无 PA9/PA10 命名，
> 且 blink_led 生成代码中唯一 UART 实例为 UART9@P208/P209。**以生成代码 P208/P209 为准**，Phase 0.5 串口实测最终确认。

## 2. 待确认清单（后续 Phase 补齐）

| 项 | 计划确认途径 |
|---|---|
| User Button 引脚 | Vision_Board_schematic.pdf |
| SWDIO/SWCLK 引脚号 | Vision_Board_schematic.pdf |
| RGB LCD 逐脚信号表（R0~R5/G0~G5/B0~B5/DE/HSYNC/VSYNC/PCLK） | 原理图 + `pin_data.c` PSEL 对照（Phase 2） |
| Camera 完整引脚（D0~D7/PCLK/VSYNC/HSYNC/XCLK/RESET/PWDN） | 原理图 + camera 工程 `omv_portconfig.h`（Phase 3） |
| Camera 默认配套 sensor 型号 | camera 工程探针顺序 + 实测（Phase 3） |
| LED 有效电平（高/低） | 原理图，必要时实测 |
