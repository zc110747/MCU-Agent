# 203.esp32s3_wireless_debug · 长期项目笔记

## 工程定位
ESP32-S3（N16R8, ESP-IDF v6.1）自研 CMSIS-DAP v1 探针固件（HID），工程 `esp32s3_debug_probe`。
目标 STM32 调试：SWD 推荐下载/仿真；JTAG 仅链路用途（下载慢=CMSIS-DAP v1 HID 协议天花板，暂不修）。
专属 skill：`stm32-cmsis-dap-probe`（SWD/JTAG FAULT/JTAG 下载限制 consolidated 主页）。

## ⚠️ ESP-IDF 沙箱构建环境（高复用，必看）
- **Git Bash 不可用**：idf.py 在 MSYS/Git-Bash 下「MSys/Mingw no longer supported」早退 → 用**原生 PowerShell 工具**（非 Bash）。
- 可用环境：IDF_PATH=`C:\esp\v6.1\esp-idf`；IDF_TOOLS_PATH=`C:\Espressif\tools`；
  IDF_PYTHON_ENV_PATH=`C:\Espressif\tools\python\v6.1\venv`；
  venv python=`C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe`；idf.py=`C:\esp\v6.1\esp-idf\tools\idf.py`；
  PATH 补 `C:\Espressif\tools\cmake\bin;C:\Espressif\tools\ninja;C:\Espressif\tools\xtensa-esp-elf\bin;C:\Espressif\tools\xtensa-esp-elf-gdb\bin`。
- **`*>` 重定向会被沙箱截断**长构建（轮次间回收→日志截 0 字节）。正确读法：前台 PowerShell 跑 `idf.py build` 并**同一命令内**接 grep 读结果；
  或 `run_in_background`+`TaskOutput block`。⚠️ `run_in_background`+`& python idf.py build *>` 2 秒假 "completed" 且 `EXIT=` 空=构建没跑，勿信。
- 前台 PowerShell 能编译（build5 曾产出 197KB 真实日志）。

## 无线调试器（最小集，2026-09-09 已写码未编译验证）
- 新增 `components/wifi_dap/`：SoftAP `ESP32-DAP-XXXX`@192.168.4.1/24 ch1 + TCP 50000 server。
- **铁律**：零改受保护文件（swd.c/jtag.c/dap.c/命令处理器/USB HID 层）；复用 `cmsis_dap_execute()` + `debug_engine_lock/unlock()`。
- 帧格式：长包头 `cmsis_dap_tcp_packet_hdr_t`（signature `0x00504144` / uint16 length / uint8 type REQ=0x01 RESP=0x02 / reserved），因 TCP 流需定界（对齐 bkuschak/cmsis_dap_tcp_esp32）。
- 验证靠 `tools/wifi_dap_client.py`（不依赖 OpenOCD）；OpenOCD 标准版无 `cmsis-dap backend tcp`，需源码编译（已留 `interface/esp32_wifi_dap.cfg` 注释）。
- 全程**未提交 git**（用户约束：确认后提交）。

## 验收节奏
Debug/Release 双构零警告 → flash(.elf) → 真机验证 → 汇报待确认提交 git。
