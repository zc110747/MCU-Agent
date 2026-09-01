# 项目长期笔记 — 201.esp32s3_rtos

## 工程约定
- **驱动目录用 `bsp/`，不要用 `drivers/`**。原因：仓库 `.gitignore` 有 `**/Drivers`（STM32 约定），小写 `drivers` 虽不严格命中，但用户明确要求本工程驱动统一放 `bsp/`，彻底规避被忽略风险。已验证 `bsp/led.cpp` 不被 gitignore。
- 模块编译方式：Arduino 构建器不自动编译子目录 `.cpp`，需在 `.ino` 中 `#include "bsp/led.cpp"` 等，把所有模块编进同一翻译单元。
- 编译 FQBN：`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600`（N16R8：OPI PSRAM + 16MB Flash，PSRAM 必须 OPI 否则 `psramFound()=false`）。

## 工具链（本机已装）
- arduino-cli：`D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe`（不在 PATH，调用用绝对路径）。
- ESP32 Core：`esp32:esp32@3.3.11`，data 目录 `D:\software\arduino-cli\data`。
- Python 3.13 已在 PATH（core 构建分区表/esptool 需要）。

## 调试工具链（随 Arduino Core 内置，无需另装）
- openocd-esp32（espressif fork，非 STM32 的 sysprogs）：`D:\software\arduino-cli\data\packages\esp32\tools\openocd-esp32\v0.12.0-esp32-20260424\bin\openocd.exe`。
- xtensa gdb：`D:\software\arduino-cli\data\packages\esp32\tools\xtensa-esp-elf-gdb\17.1_20260402\bin\xtensa-esp32-elf-gdb.exe`。
- **调试接口**：ESP32-S3 内置 USB-Serial-JTAG = VID 0x303a PID 0x1001（板载 USB 口，用 `board/esp32s3-builtin.cfg`，免外接调试器）；CH343 串口口只能下载不能调试；外接 ESP-Prog 用 `board/esp32s3-ftdi.cfg`。Cortex-Debug 走 openocd 后端即可调试 xtensa 核。
- 固件 elf（含调试符号）：`.build/201.esp32s3_rtos.ino.elf`（elf32-xtensa-le）。

## 板级硬件（2026-09-01 实测，ESP32-S3-COREBOARD V1.4 用户板）
- **用户 LED = WS2812B RGB，GPIO48**（单线 800kHz，GRB 字节序），真实可控。**不能用 digitalWrite**；驱动用 Adafruit_NeoPixel（NEO_GRB+NEO_KHZ800，1 pixel，内部走 RMT）。依赖 `Adafruit NeoPixel@1.15.5`（`arduino-cli lib install "Adafruit NeoPixel"`）。`config.h` 用 `LED_IS_WS2812` 宏切 WS2812/普通数字 LED 驱动；`led_set(bool)` 把 on→GRB 颜色 / off→黑，上层任务/UART 命令不变。
- **BOOT 按键 = GPIO0**（active-low，R5 10k 上拉）。板子无 GPIO2 用户 LED（GPIO2 仅自动下载上拉）。
- 烧录/调试口 = 板载 USB-Serial-JTAG（VID 0x303a PID 0x1001）→ 本机当前 **COM22**（口会变，勿硬编码）。该口既是 esptool upload 口也是 openocd 调试口。
- 硬件灯（不可控）：PWRLED-RED（常亮）、TXLED2/RXLED2（UART 活动）。
- 真机验证验收：编译 0/0 warning → pyserial 识别 303A:1001 口 → `flash-esp32.bat COM22 --no-pause`（原生 PowerShell 跑）→ `[FLASH] PASS`+`Hash verified` → pyserial @115200 读启动横幅/`led on/off/toggle` 回显 → GPIO48 WS2812B 实际亮灭（功能 ok ✅）。详细配方见 skill `esp32-board-hardware`。

