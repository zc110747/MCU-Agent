# Phase 0 验收报告

日期：2026-09-30

## 1. 完成项

| # | 项 | 状态 | 证据 |
|---|---|---|---|
| 1 | 官方 BSP 目录结构摸底（documents/projects/libraries/rt-thread） | ✅ | 遍历记录见会话/下表 |
| 2 | MCU：R7FA8D1BH，Cortex-M85 @480MHz，2MB Flash/1MB SRAM/64KB ITCM+DTCM | ✅ | `bsp_clock_cfg.h`、`memory_regions.ld` |
| 3 | 时钟树：XTAL 20MHz → PLL 960MHz → CPU 480MHz；LCDCLK=240MHz | ✅ | `bsp_clock_cfg.h` |
| 4 | 内存映射（Flash/SRAM/SDRAM/QSPI/ITCM/DTCM/Option） | ✅ | `memory_regions.ld` |
| 5 | Debug UART = UART9 @ P208/P209，115200 8N1 | ✅ | blink_led `hal_data.c`/`pin_data.c` |
| 6 | LED = P102 | ✅ | blink_led `hal_entry.c` |
| 7 | RGB LCD 4.3"：800×480 RGB565，双缓冲放 SDRAM `.sdram` 段，stride 1664B | ✅ | `common_data.h/.c`、`fsp.ld` |
| 8 | SDRAM：32MB @0x68000000，16bit，完整时序参数 | ✅ | `drv_sdram.c`、rtconfig.h |
| 9 | Cache/DMA：`.nocache` / `.nocache_sdram` 链接段机制确认 | ✅ | `fsp.ld` |
| 10 | Camera：CEU 8-bit + SCCB，支持 12 种 sensor 驱动 | ✅ | camera 工程 sensors/ |
| 11 | Touch GT9147：INT=P010、RST=P000 | ✅ | rgb 工程 `hal_entry.c` |
| 12 | LVGL 来源：官方 BSP 自带 LVGL v9.1.0（`libraries/components/`） | ✅ | lvgl 工程SConstruct |
| 13 | 本机工具链验证 | ✅ | 见下 |

工具链实测：
```text
arm-none-eabi-gcc 15.3.1 20260627 (Arm GNU Toolchain 15.3.Rel1)  ✅ 版本符合要求
arm-none-eabi-gdb 16.3.90                                        ✅
OpenOCD 0.12.0 (sysprogs 2026-01-21)                             ✅
CMake 4.2.1 / Ninja 1.13.2                                       ✅
```

## 2. 官方 BSP 关键参考工程定位

| 用途 | 路径（BSP 内） |
|---|---|
| 最小工程（LED+UART） | `projects/vision_board_blink_led` |
| RGB LCD 工程 | `projects/lcd/vision_board_rgb_4.3inch` |
| LVGL 工程（移植参考） | `projects/lvgl/vision_board_rgb_4.3inch_lvgl` |
| Camera 工程（Phase 3 参考） | `projects/vision_board_camera` |
| FSP 驱动 + CMSIS 设备文件 | `projects/*/ra/fsp/src/`（含 `bsp/cmsis/Device/RENESAS/Include/R7FA8D1BH.h`） |
| LVGL 源码 | `libraries/components/LVGL-v9.1.0` |

## 3. 未决项（不阻塞 Phase 0.5，按阶段关闭）

| # | 未决项 | 关闭阶段 |
|---|---|---|
| 1 | yaml "PA9/PA10" 与生成代码 P208/P209 冲突 → 以 P208/P209 为准，串口实测确认 | Phase 0.5 |
| 2 | yaml "32Mb-SDRAM" 与代码 0x2000000(32MB) 冲突 → 以代码为准，SDRAM 读写实测确认 | Phase 0.5/1 |
| 3 | User Button 引脚、SWD 引脚号、LED 有效电平 | 查原理图 PDF |
| 4 | RGB LCD 逐脚信号表（数据线/DE/HSYNC/VSYNC/PCLK） | Phase 2 |
| 5 | Camera 完整引脚 + 默认 sensor 型号 | Phase 3 |
| 6 | OpenOCD 对 RA8D1 (Cortex-M85) 的 target/flash 支持实测 | Phase 0.5 |
| 7 | Cortex-M85 的 -mcpu/-mfpu/-mfloat-abi 参数实测确定 | Phase 0.5 |
| 8 | RT-Thread Nano 获取方式（独立下载 vs 从 full 5.0.2 提取内核核心） | Phase 1 计划时定 |

## 4. 结论

Phase 0 通过。硬件事实全部带来源落档，两处官方资料冲突已显式记录并以代码证据为准。
**下一步：Phase 0.5（GCC 15.3.1 Cortex-M85 最小裸机工程 + OpenOCD/ART-Link 调试链实测）。**
