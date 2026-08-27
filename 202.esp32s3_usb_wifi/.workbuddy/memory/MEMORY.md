# 项目记忆: ESP32-S3 USB Wi-Fi RNDIS 网卡

## 目标
ESP32-S3 通过原生 USB Device (GPIO19=USB D-, GPIO20=USB D+) 枚举为 Windows RNDIS 网络适配器，
Wi-Fi STA 联网，NAT 共享给 Windows，Web 页面 (http://192.168.4.1) 配置 Wi-Fi。

## 工具链 (实测，2026-08-26 纠正)
- ESP-IDF 由用户自装，位置 `D:/esp/esp-idf`（沙箱实测存在但**当时不完整**：缺根 `export.bat`、
  `components/tinyusb` 未就位）。一键脚本默认指向此路径，可用环境变量 `IDF_PATH` 覆盖。
- 工具链路径 `IDF_TOOLS_PATH` 由用户系统环境变量 / `export.bat` 提供（脚本不再写死）。
- `D:/software/esp32-tools` 实际是 **ESP32 Arduino 工具链**（arduino-cli 1.5.1 + ESP32 core 3.3.11），
  仅用于 Arduino 草图，**不能**编译本 ESP-IDF 工程。
- 目标芯片: esp32s3
- 用户原计划装到 D:/software/esp32-tools/esp-idf，但实测安装落在 D:/esp/esp-idf，以实测为准。

## 硬件
- 下载口: COM21 (CH343)，`idf.py -p COM21 flash`
- USB D-/D+: GPIO19/GPIO20 (ESP32-S3 原生 USB Device，不当普通 GPIO)

## 目录约定 (用户要求，便于跨工程共享/避免膨胀)
- `third_party/`  : 第三方/外置代码（如 NAT 组件、RNDIS 外置实现）
- `Drivers/`      : 官方/SDK 库与板级驱动（USB 描述符、板级初始化）
- 二者已在顶层 CMakeLists.txt 通过 `set(EXTRA_COMPONENT_DIRS third_party Drivers)` 注册
- app 业务代码仍在 `main/` (usb/wifi/network/web/config/system)

## 关键实现点 (待 IDF 落地后核对真实 API)
- RNDIS: TinyUSB `CFG_TUD_ECM_RNDIS` + `tud_network_*` 回调；控制面 `rndis_class_set_handler` 需应用实现
- NAT: ESP-IDF lwIP `ip_napt` (sdkconfig `CONFIG_LWIP_IPV4_NAPT=y`)；USB 侧 192.168.4.1 + DHCP Server
- Wi-Fi 凭据存 NVS，密码不打印日志

## 验收脚本
- `tools/verify_rndis.py` (Windows, netsh/ping/nslookup) 端到端验收 RNDIS 网卡
- `tools/run.bat|sh` 一键 编译+烧录+监视(COM21，IDF 默认 D:/esp/esp-idf，可 IDF_PATH 覆盖)
- `tools/build_all.bat|sh` 仅完整编译
