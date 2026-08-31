# 项目长期笔记 — 201.esp32s3_rtos

## 工程约定
- **驱动目录用 `bsp/`，不要用 `drivers/`**。原因：仓库 `.gitignore` 有 `**/Drivers`（STM32 约定），小写 `drivers` 虽不严格命中，但用户明确要求本工程驱动统一放 `bsp/`，彻底规避被忽略风险。已验证 `bsp/led.cpp` 不被 gitignore。
- 模块编译方式：Arduino 构建器不自动编译子目录 `.cpp`，需在 `.ino` 中 `#include "bsp/led.cpp"` 等，把所有模块编进同一翻译单元。
- 编译 FQBN：`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600`（N16R8：OPI PSRAM + 16MB Flash，PSRAM 必须 OPI 否则 `psramFound()=false`）。

## 工具链（本机已装）
- arduino-cli：`D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe`（不在 PATH，调用用绝对路径）。
- ESP32 Core：`esp32:esp32@3.3.11`，data 目录 `D:\software\arduino-cli\data`。
- Python 3.13 已在 PATH（core 构建分区表/esptool 需要）。
