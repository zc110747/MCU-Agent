# 102.stm32f429_tinyusb_ui — 长期项目记忆

## 硬件平台
- MCU: STM32F429IGT6, HSE 25MHz, 内部 SRAM 256KB / SDRAM(W9825G6KH-6 32MB) 经 FMC.
- 调试串口: **USART3 (PB10/PB11) @ 115200 8N1**, 本机枚举为 **COM5** (用户确认 COM5 即通讯串口).
- 烧录: OpenOCD + ST-Link; openocd 在 `D:/software/ST/OpenOCD/bin/openocd.exe`, scripts 在 `D:/software/ST/OpenOCD/share/openocd/scripts`; cfg=`interface/stlink.cfg`+`target/stm32f4x.cfg`.
- USB: TinyUSB Host (U 盘 MSC), FatFs 挂 `0:`; 字库在 `0:/SYSTEM/FONT/` (GBK12/16/24/32.FON + UNIGBK.BIN).
- LCD: **800x480** 正点原子, FMC Bank1 NE1 8080 16-bit (RS=A18, LCD_BASE=0x60000000|0x0007FFFE); 控制器 NT35510(0x8000)/ILI9806E 回退.
- **`lcd_scan_dir` 的宽高交换逻辑是 正点原子 原版、正确，切勿改反/删除**: `DFT_SCAN_DIR=L2R_U2D`(MV=0) 下, 因 `lcd_width(800)>lcd_height(480)` 触发交换 → **有效 GRAM 窗口 480x800**, 这正是 NT35510 模块铺满物理 800x480 屏所需的窗口. 屏幕尺寸只由 `LCD_WIDTH/LCD_HEIGHT`(`bsp/bsp_lcd.h`) 与 `UI_W/UI_H`(`app/app_ui.c`) 决定; 改尺寸时只动这两个宏, 不要动交换逻辑.

## 工作流约定（用户明确指令）
- **每次改完代码 → 直接构建 + OpenOCD 烧录 + COM5 串口真机验证，不再只交付 ELF 让用户手动跑**。环境（COM5/OpenOCD/ST-Link）已就绪。
- 验收铁律（见 stm32-verification-acceptance skill）: 先计划后编码 → Debug/Release 双构零警告(显式列 RAM/FLASH 占比) → 真机烧录(Verified OK) → verify 脚本 pass/fail → 增量交付清单(✅ 收尾) → README 更新。
- 真机验证命令: `SERIAL_PORT=COM5 python verify_serial/verify_ui_com5.py` (脚本内含烧录+抓串口); 或纯烧录用 openocd `flash write_image erase ...` + `verify_image` + `reset run`.

## 已知坑
- `lv_conf.h` 曾把 `LV_MEM_ADR` 重定义成 0（会让 LVGL 在内部 SRAM 塞 256KB 静态 BSS），必须保持 `0xC0100000U` (SDRAM, 避开 FreeRTOS 堆 0xC0000000~0xC007FFFF)。
- `ffconf.h` 须 `FF_FS_EXFAT 1` (U 盘 exFAT 才能挂载字库)。
- `stm32f4xx_hal_conf.h` 须开 `HAL_SRAM_MODULE_ENABLED` (FMC TFT 依赖)。
- LCD 地址窗口必须用 MIPI-DCS 时序: 命令写一次 + 跟 4 数据字节; 不可把 0x2A/0x2B/0x2C 当连续寄存器拆写 (否则渲染带写到错乱 GRAM → 文字重叠)。
- 本机 COM5 曾出现驱动 code-31 (设备未发挥作用); 用户侧已恢复, 视作可用.
- **OpenOCD 烧录必须用 `.elf`**(自带加载段), 用 `.bin` 会报 `no flash bank found for address 0x00000000` 且 `wrote 0 bytes`. 命令: `flash write_image erase build/stm32f429_tinyusb_ui.elf` + `verify_image` + `reset run`. (注意 Git-Bash 里 cd 路径必须用正斜杠, 反斜杠会吞掉目录分隔.)
