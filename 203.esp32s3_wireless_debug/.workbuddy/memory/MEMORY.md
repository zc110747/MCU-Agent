# 203.esp32s3_wireless_debug · 长期项目笔记

## 工程定位
ESP32-S3（N16R8, ESP-IDF v6.1）自研 CMSIS-DAP v1 探针固件（HID），工程 `esp32s3_debug_probe`。
目标 STM32 调试：SWD 推荐下载/仿真；JTAG 仅链路用途（下载慢=CMSIS-DAP v1 HID 协议天花板，暂不修）。
专属 skill：`stm32-cmsis-dap-probe`（SWD/JTAG FAULT/JTAG 下载限制 consolidated 主页）。

## ⚠️ ESP-IDF 构建环境（2026-09-10 重写，旧结论作废）
- **PowerShell 工具在本沙箱不可用**：`&` 调原生 exe 输出被吞（`python --version` 都拿不到），
  `Start-Process -RedirectStandardOutput` 也是 0 字节 + 空 ExitCode，`dangerouslyDisableSandbox` 无效。
  cmdlet（Test-Path/Get-PnpDevice/写文件）能跑，但**子进程 stdout 全丢**。
- **正确做法**：**Git Bash + 工程自带 `./build.sh`**（env.sh 已 `unset MSYSTEM` 并配好 PATH/IDF_PATH），
  配 `run_in_background` + `TaskOutput block` 等真实结束，日志落 `logs/*.log` 再 grep。5 min 级构建稳定。
  烧录同理：`source ./env.sh && "$PY" idf_runner.py flash -p COM21 -b 460800`。
- 环境：IDF_PATH=`D:\data\agent-tools\esp32\v6.1\esp-idf`（**已从 C:\esp 迁移**）；
  IDF_TOOLS_PATH=`C:\Espressif\tools`；venv python=`C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe`。
- 串口识别用 pyserial（Get-WmiObject/GetPortNames 漏 USB 串口）；本项目常驻 COM21（CH343）。
- sdkconfig 改 Kconfig 后，若改了 CMakeLists 需 `idf_runner.py reconfigure` 再 build（否则用旧缓存）。

## ★ 传输仲裁：USB 优先，Wi-Fi AP 兜底（2026-09-10 落地）
- **政策**：两传输互斥。上电先起 USB，在 `CONFIG_WIFI_DAP_USB_WAIT_MS`（默认 3000 ms）内等主机枚举；
  枚举成功 → 常驻 USB 模式，**完全不启动 esp_wifi**；超时无主机 → 才起 SoftAP `ESP32-DAP-XXXX`
  @192.168.4.1 + TCP 50000。
- **为何互斥**：ESP32-S3 上 Wi-Fi RF 破坏 USB OTG HID 时序 → 主机侧 CMSIS-DAP 通讯错误。
  历史事故：`CONFIG_DEBUG_ENABLE_WIFI` 存在但**代码里没人用** → wifi_dap 无条件启动 → USB 挂。
- **判定必须用 `tud_mounted()`，不能用 `tud_connected()`**：后者在 ESP32-S3 只代表 **VBUS 存在**
  （插充电头也为真），会误判成"有主机"而永不开启 AP。已封装 `usb_device_is_connected()` /
  `usb_device_vbus_present()`。
- **`CONFIG_DEBUG_ENABLE_WIFI` 只能用于 C 的 `#if`，不能用于 CMake 的 `if(CONFIG_...)`**：
  本 IDF 版本组件注册阶段取不到该变量，条件化 REQUIRES 会静默丢 include 路径
  （报 `wifi_dap.h: No such file` + `component_requirements.py BUG`）。已在 main/CMakeLists.txt 注释说明。
- 验证三件套：① 串口 W 级仲裁日志；② `openocd -f tools/verify_usb_hid.cfg`（不需目标板，
  见到 `FW Version = 1.2.0` + `Interface ready` 即 OK，末尾 `Error connecting DP` 是没接目标的预期）；
  ③ `netsh wlan show networks mode=bssid | grep ESP32-DAP` 应为 0（Windows 扫描有缓存，等 20–30 s）。

## ★ 本项目日志坑（必看）
- `CONFIG_LOG_DEFAULT_LEVEL=2`（**WARN**）→ 所有 `ESP_LOGI/D/V` **不可见**（有意：UART 日志不能成为
  HID 下载瓶颈）。启动/仲裁类关键日志一律用 `ESP_LOGW` 才能看到。
- 上电可见的 `gpio: conflict found for GPIO[4]/[5]` = SWD 的 SWCLK/SWDIO 被重复配置，既有告警，非故障。
- 加 WiFi 后固件 805~806 KB（WiFi/lwIP 占约 +597 KB），app 分区 1 MB，余量约 23%。

## 无线调试器（最小集）
- `components/wifi_dap/`：SoftAP + TCP 50000 server，**仅无 USB 主机时启动**。
- **铁律**：零改受保护文件（swd.c/jtag.c/dap.c/命令处理器/USB HID 层）；复用 `cmsis_dap_execute()` + `debug_engine_lock/unlock()`。
- 帧格式：长包头 `cmsis_dap_tcp_packet_hdr_t`（signature `0x00504144` / uint16 length / uint8 type REQ=0x01 RESP=0x02 / reserved）。
- 验证靠 `tools/wifi_dap_client.py`；OpenOCD 标准版无 `cmsis-dap backend tcp`，需源码编译
  （已留 `interface/esp32_wifi_dap.cfg` 注释）。
- git：无线功能已提交（730ce66, 2026-09-09 23:09）；**2026-09-10 的传输仲裁改动未提交**，等用户确认。

## 验收节奏
Debug/Release 双构零警告 → flash → 真机验证 → 汇报待确认提交 git（git push 由用户自行处理）。
