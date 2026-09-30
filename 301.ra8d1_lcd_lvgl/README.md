# 301.ra8d1_lcd_lvgl — RA8D1 Vision Board (RT-Thread Nano + LVGL)

基于 **Renesas RA8D1 Vision Board**（Cortex-M85 @480MHz）的干净嵌入式工程：
Arm GNU GCC 15.3.1 + CMake/Ninja + OpenOCD + CMSIS-DAP(ART-Link)，CLI 可构建、可烧录、可调试。

## 硬件速览

- MCU：R7FA8D1BH（Cortex-M85，480MHz，2MB Flash，1MB SRAM，64KB ITCM/DTCM）
- SDRAM 32MB @0x68000000；QSPI Flash 1MB @0x60000000
- Debug UART：UART9 @ P208/P209，115200 8N1；LED：P102
- LCD：RGB 4.3" 800×480 RGB565（GLCDC，framebuffer 在 SDRAM）/ MIPI-DSI 可选
- Camera：CEU 8-bit DVP + SCCB；Touch：GT9147

详见 `documents/`（hardware / pinmap / memory-map / architecture / phase0-report）。

## 目录

```text
applications/   应用层
bsp/            板级适配层（startup/linker/uart/lcd/camera/lvgl port）
Drivers/        Renesas FSP/HAL 原始驱动（从官方 BSP 拷贝，保持原样）
third_party/    rt-thread-nano、lvgl（上游原样）
documents/      分析文档与阶段验收报告
tools/          build/flash/debug/clean CLI 脚本 + openocd cfg
```

依赖方向：`applications → bsp → { Drivers, third_party }`，禁止反向。

## 阶段状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| Phase 0 | 硬件与官方 BSP 分析 | ✅ |
| Phase 0.5 | GCC 裸机最小工程 + OpenOCD 调试链 | ⏳ |
| Phase 1 | RT-Thread Nano + LED + UART | ⏳ |
| Phase 2 | LCD（GLCDC，无 LVGL） | ⏳ |
| Phase 3 | Camera（CEU） | ⏳ |
| Phase 4 | LVGL v9.1.0 | ⏳ |

## 参考

官方 BSP（只读参考，不参与构建）：`E:/cnb/git/sdk-bsp-ra8d1-vision-board-master`
（https://github.com/RT-Thread-Studio/sdk-bsp-ra8d1-vision-board）
