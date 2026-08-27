# 102.stm32f429_tinyusb_ui — 长期项目记忆

## 硬件平台
- MCU: STM32F429IGT6, HSE 25MHz, 内部 SRAM 256KB / SDRAM(W9825G6KH-6 32MB) 经 FMC.
- 调试串口: **USART3 (PB10/PB11) @ 115200 8N1**, 本机枚举为 **COM5** (用户确认 COM5 即通讯串口).
- 烧录: OpenOCD + ST-Link; openocd 在 `D:/software/ST/OpenOCD/bin/openocd.exe`, scripts 在 `D:/software/ST/OpenOCD/share/openocd/scripts`; cfg=`interface/stlink.cfg`+`target/stm32f4x.cfg`.
- USB: TinyUSB Host (U 盘 MSC), FatFs 挂 `0:`; 字库在 `0:/SYSTEM/FONT/` (GBK12/16/24/32.FON + UNIGBK.BIN).
- LCD: 800x400 正点原子, FMC Bank1 NE1 8080 16-bit (RS=A18, LCD_BASE=0x60000000|0x0007FFFE); 控制器 NT35510(0x8000)/ILI9806E 回退.

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
