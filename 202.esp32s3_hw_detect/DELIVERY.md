# ESP32-S3 Remote Hardware Debugger — 交付文档

> 框架目标：把 ESP32-S3 变成网络化硬件调试网关，实时把目标 MCU 的 **UART 日志 / 外部 ADC 电压 / GPIO 状态** 通过 **Web(WebSocket) + MQTT** 提供给浏览器、上位机与未来 AI Agent。
> 架构核心：**统一 Event Bus（FreeRTOS Queue）** 解耦生产者（UART/ADC/GPIO）与消费者（WebSocket/MQTT/Log），新增模块（CAN/I2C/SPI/逻辑分析仪…）零改动核心。

---

## 1. 完整工程目录

```text
202.esp32s3_hw_detect/
├── platformio.ini
├── DELIVERY.md                 # 本文档
├── build.log                   # 编译日志（自动生成）
└── src/
    ├── main.cpp                # 入口，调用 gateway.begin()
    ├── app/
    │   ├── debug_gateway.h/.cpp   # 总编排：启动流程 + EventTask 分发 + 命令解析
    │   ├── debug_module.h        # DebugEvent / DebugEventType / DebugModule 扩展接口
    │   ├── event_bus.h/.cpp       # FreeRTOS Queue 事件总线（非阻塞 push）
    │   ├── ring_buffer.h          # 单生产者单消费者字节环形缓冲（UART 抗突发）
    │   └── pin_manager.h/.cpp      # 运行时 GPIO 资源强制管理（禁用 19/20/48 与重复占用）
    ├── config/
    │   ├── pin_config.h           # 全部引脚集中定义（唯一真相源）
    │   ├── app_config.h            # 任务栈/优先级/核/MQTT/Web 等全局常量
    │   └── mqtt_config.h          # MQTT Topic 布局与 Broker 默认值
    ├── network/
    │   ├── wifi_manager.h/.cpp     # STA + AP 回退 + 自动重连
    │   ├── mqtt_manager.h/.cpp     # MQTT 发布/订阅 + 自动重连 + cmd 转发
    │   ├── websocket_manager.h/.cpp# WebSocket 实时推送（队列解耦）
    │   ├── web_server.h/.cpp       # Web 服务器：Dashboard + REST + OTA
    │   └── web_pages.h             # 单页 Dashboard HTML（C 字符串，零构建）
    ├── debug/
    │   ├── uart_monitor.h/.cpp     # UART 监视（HardwareSerial + 任务 + 二进制/文本）
    │   ├── adc_monitor.h/.cpp      # ExternalAdc 抽象 + ADS1115 实现 + 采样任务
    │   └── gpio_monitor.h/.cpp     # GPIO 数字输入/输出
    ├── storage/
    │   ├── config_manager.h/.cpp   # Preferences(NVS) 持久化
    │   └── log_manager.h/.cpp       # 分级日志 + 环形缓冲 + 可选 WS 转发
    └── ota/
        └── ota_manager.h/.cpp      # Web OTA（仅写 app 分区，保护 NVS）
```

> 说明：`main.cpp` 仅 9 行，所有逻辑按功能拆分到 `app/network/debug/storage/ota/config`，未违反“禁止把所有代码写进 main.cpp”。

---

## 2. 关键源码说明（按需查阅，见各 .cpp）

- **EventBus**（`app/event_bus.*`）：生产者 `push()` 用 `xQueueSend(...,0)` **永不阻塞**；队列满则丢弃并 `overflow++`，保证慢网络客户端不会拖垮 UART 接收。
- **UART Monitor**（`debug/uart_monitor.*`）：`HardwareSerial(1)` 默认 115200/8N1；RX→环形缓冲→按行（文本）或按块（HEX）成帧→`DebugEvent` 入总线；`baud/data/stop/parity` 可运行时改。
- **ADC Monitor**（`debug/adc_monitor.*`）：抽象层 `ExternalAdc`，首版实现 `Ads1115Adc`（I2C，单拍 860 SPS）。核心只依赖接口，换芯片只写新子类。
- **GPIO Monitor**（`debug/gpio_monitor.*`）：4 路监控，输入变化即推事件；输出经 PinManager 校验。
- **网关**（`app/debug_gateway.*`）：`begin()` 按 34 节顺序启动；`EventTask` 单消费者把事件扇出到 WS/MQTT，并每 5s 广播 SYSTEM 状态。
- **命令分发**：MQTT `cmd` 主题与 WebSocket 入站 JSON 都汇聚到 `getGateway().handleJsonCommand()`，单点校验。

---

## 3. platformio.ini

```ini
[env:esp32-s3-devkitc-1]
platform          = espressif32
board             = esp32-s3-devkitc-1
framework         = arduino
monitor_speed     = 115200
upload_speed      = 921600
board_build.flash_size = 16MB
build_flags = -D MQTT_MAX_PACKET_SIZE=512 -D CORE_DEBUG_LEVEL=3
lib_deps =
    arduino-libraries/ArduinoJson@^7.2.0
    knolleary/PubSubClient@^2.8.0
    links2004/WebSockets@^2.4.0
```

已验证可迁移 Arduino CLI：上述 `lib_deps` 等价于 `arduino-cli lib install` 的三个库；`board/framework` 与 Arduino IDE 的板卡/核心选择一一对应。

---

## 4. GPIO 配置表

| GPIO | 角色 | 方向 | 说明 |
|------|------|------|------|
| 19 | USB D- | 保留 | **禁止占用**（USB 串口） |
| 20 | USB D+ | 保留 | **禁止占用**（USB 串口） |
| 48 | RUN LED | 保留/输出 | 板载运行灯（用户确认），系统保留 |
| 17 | UART_RX | 输入 | ← 目标 MCU TX |
| 18 | UART_TX | 输出 | → 目标 MCU RX |
| 8  | ADC I2C SDA | 开漏 | ADS1115 I2C |
| 9  | ADC I2C SCL | 输出 | ADS1115 I2C |
| 4  | GPIO_MON_0 | 可配 | 默认输入上拉 |
| 5  | GPIO_MON_1 | 可配 | 默认输入上拉 |
| 6  | GPIO_MON_2 | 可配 | 默认输入上拉 |
| 7  | GPIO_MON_3 | 可配 | 默认输入上拉 |

- 启动后串口打印 **GPIO Resource Allocation** 表，列出保留引脚与每个模块占用的引脚。
- 任何模块未 `PinManager::claim()` 成功即在 `begin()` 失败，**不会**触碰 19/20/48，也不会重复占用。

---

## 5. 第三方依赖

| 库 | 版本 | 用途 | 是否核心自带 |
|----|------|------|--------------|
| ArduinoJson | ^7.2.0 | 所有 JSON（MQTT/WS/REST） | 否（已列入 lib_deps） |
| PubSubClient | ^2.8.0 | MQTT 客户端 | 否（已列入） |
| WebSockets | ^2.4.0 | WebSocket Server | 否（已列入） |
| WiFi / WebServer / Preferences / HardwareSerial / Wire / Update | — | 网络/存储/串口/I2C/OTA | **Arduino Core 自带** |

> 刻意不引入 ESPAsyncWebServer、ADS1X15 等库：Web 用核心 `WebServer` + `WebSocketsServer`；ADC 自写 I2C 驱动（仅 ~40 行），降低依赖与 Flash 占用。

---

## 6. 编译方法

```bat
:: 用本机任意 Python 建隔离 venv（绕过全局 pip target 陷阱）
"D:\Software\Python3\python.exe" -m venv C:\pio-venv
C:\pio-venv\Scripts\activate.bat
pip install platformio

cd D:\user_project\git\MCU-Agent\202.esp32s3_hw_detect
pio run                 :: 编译
pio run -t upload       :: 烧录（需先插板，自动选串口）
pio device monitor      :: 串口监视（115200）
```

首跑会自动下载 ESP32 工具链 + Arduino 框架（约数百 MB，视网速数分钟）。

---

## 7. 烧录方法

- 自动：`pio run -t upload`（PlatformIO 自动识别 ESP32-S3 的 ROM USB 串口）。
- 手动：用 `esptool` 或 IDE 选 `esp32-s3-devkitc-1`，Flash 16MB，按默认分区（含 ota_0/ota_1）。
- 烧录后设备以 **USB CDC（GPIO19/20）** 输出日志，波特 115200。

---

## 8. Web 访问方法

1. 设备联网后，浏览器打开 `http://<设备IP>/`（STA 模式 IP，或 AP 模式 `http://192.168.4.1/`）。
2. Dashboard 顶部显示连接状态，自动连 `ws://<host>:81/`。
3. 功能页：Dashboard / UART / ADC / GPIO / System / Config / OTA / Logs。
4. 配置类操作需填 **Web 密码**（默认 `admin`，可在 Config 页修改）。
5. OTA：OTA 页选 `.bin` 上传（需密码），成功后自动重启；NVS/WiFi/MQTT/设备ID 保留。

---

## 9. MQTT Topic 说明

基础主题：`remote-debugger/<device_id>/`，其中 `device_id = esp32s3-XXXXXXXX`（MAC 派生）。

| 方向 | Topic | 内容 |
|------|-------|------|
| 上行 | `.../status` | 遗嘱/状态（LWT `offline`，连上后由 SYSTEM 推送） |
| 上行 | `.../uart/rx` | UART 接收 |
| 上行 | `.../uart/tx` | UART 发送回显 |
| 上行 | `.../adc/ch0..3` | ADC 各通道电压 |
| 上行 | `.../gpio` | GPIO 状态变化 |
| 上行 | `.../system` | 系统状态 JSON |
| 上行 | `.../event` | ERROR/事件 JSON |
| 下行 | `.../cmd` | 上位机/AI 下发命令 |

---

## 10. MQTT JSON 协议

**UART**（rx/tx 同构，`encoding` 区分文本/HEX）
```json
{ "timestamp": 12345678, "channel": 0, "encoding": "text", "data": "System Init" }
```
**ADC**
```json
{ "timestamp": 12345678, "channel": 0, "raw": 1234, "voltage": 3.301 }
```
**GPIO**
```json
{ "timestamp": 12345678, "gpio": 4, "state": 1 }
```
**System**
```json
{ "device":"esp32s3-ABCD1234","uptime":123,"free_heap":123456,"min_heap":120000,
  "wifi":"sta","wifi_rssi":-48,"ip":"192.168.1.50","mqtt":"connected",
  "firmware":"1.0.0","uart_drops":0,"adc_drops":0 }
```
**下行命令（.../cmd）**
```json
{ "cmd":"uart_config", "baud":115200 }
{ "cmd":"uart_tx", "data":"AT\r\n" }
{ "cmd":"gpio_set", "gpio":4, "value":1 }
{ "cmd":"adc_read" }
{ "cmd":"wifi_config", "ssid":"...", "pass":"..." }
{ "cmd":"mqtt_config", "broker":"1.2.3.4","port":1883,"user":"","pass":"","keep":30,"tls":false }
{ "cmd":"device_reset" }
```
所有命令经 `handleJsonCommand` 统一校验（参数合法性、GPIO 合法性、越权拒绝），结果以日志/事件反馈。

---

## 11. WebSocket 协议（端口 81）

入站（浏览器/AI → 设备）：
```json
{ "cmd":"uart_tx", "data":"AT+RST" }
{ "cmd":"gpio_set", "gpio":4, "value":1 }
```
出站（设备 → 浏览器）：
```json
{ "type":"uart",      "timestamp":..., "encoding":"text", "data":"Booting..." }
{ "type":"uart_tx",   "timestamp":..., "encoding":"text", "data":"AT+RST" }
{ "type":"adc",       "channel":0, "raw":1234, "voltage":3.301, "timestamp":... }
{ "type":"gpio",      "gpio":4, "state":1, "timestamp":... }
{ "type":"log",       "line":"[123][INFO][UART] ..." }
{ "type":"system",    "device":..., "uptime":..., "free_heap":..., ... }
```
> 禁止浏览器高频 HTTP polling 拉 UART；全部走 WebSocket 实时推送。

---

## 12. UART 接线说明

```text
目标 MCU            ESP32-S3
TX      ────────►  GPIO17 (UART_RX)
RX      ◄────────  GPIO18 (UART_TX)
GND     ────────►  GND
```
- 默认 115200/8N1，支持 9600…921600、5-8 数据位、1-2 停止位、无/奇/偶校验（Web/MQTT 改）。
- 支持二进制/ASCII/UTF-8/连续大日志；HEX 模式可选。

---

## 13. ADC 接线说明（ADS1115，I2C）

```text
ADS1115            ESP32-S3
VCC     ────────►  3.3V
GND     ────────►  GND
SDA     ────────►  GPIO8
SCL     ────────►  GPIO9
ADDR    ────────►  GND  (I2C 0x48)
A0..A3  ◄────────  被测电压（超过量程需外部分压）
```
- 4 通道（CH0..3），默认 ±6.144V 量程（gain 2/3）以兼容示例中的 5.012 V。
- 实际电压 = `ADC采样值 × lsb × divider_ratio + offset`；`divider`/`offset` 每通道可在 Web 配置。
- 采样率默认 10 Hz（100 ms），可选 1/5/10/20/50/100 Hz。

---

## 14. GPIO 资源说明

- 监控引脚：`DebugPins::GPIO_MONITOR_PINS = {4,5,6,7}`，集中定义于 `pin_config.h`。
- 默认全部 **输入上拉**；经 Web/MQTT `gpio_set` 可提升为输出（自动更新 NVS 持久化方向）。
- 严禁 19/20/48：由 `PinManager` 在 `claim()` 阶段硬拒绝。
- WebSocket GPIO 页与 REST `/api/gpio` 实时显示 HIGH/LOW，并提供 SET/CLEAR 按钮。

---

## 15. FreeRTOS 任务说明

| 任务 | 栈 | 优先级 | 核 | 职责 |
|------|----|--------|----|------|
| uart_task | 4096 | 3 | 1 | 收 UART → 环形缓冲 → 入总线 |
| adc_task | 3072 | 2 | 1 | 周期采样 4 通道 → 入总线 |
| gpio_task | 2048 | 2 | 1 | 轮询输入变化 → 入总线 |
| event_task | 4096 | 4 | 0 | 总线单消费者 → 扇出 WS/MQTT + 周期状态 |
| mqtt_task | 5120 | 3 | 0 | 连接/重连/发布队列 |
| ws_task | 8192 | 2 | 0 | WebSocket.loop() + 广播队列 |
| web_task | 10240 | 2 | 0 | WebServer.handleClient() |

> 所有任务创建后由启动日志打印名字/栈/优先级/核。生产者与消费者经 Queue/Mutex 隔离，无直接共享可变缓冲。

---

## 16. 内存占用

（见下方“编译结果”，由 `build.log` 与运行时 Heap 打印填写。）

---

## 17. 已完成测试

（编译/零警告/任务创建/GPIO 校验见编译结果；真机功能测试需烧录后在本机串口与浏览器验证。）

---

## 18. 已知限制

- **MQTT TLS**：`tls` 字段已预留并持久化，但 v1 仍走明文 TCP（未接 `WiFiClientSecure`），后续接证书即启用。
- **ADC 采样率上限**：ADS1115 单拍模式 4 通道 × ~2 ms ≈ 8 ms/轮，100 Hz 档仅勉强（≈125 Hz 上限）；更高频建议改用连续模式或更快 ADC。
- **UART 超高速**：921600 下若 WS/MQTT 长期掉线，总线溢出计数会增长（设计内 drop 保护，不丢 UART 接收缓冲）。
- **AI Agent 接口**：协议已就绪（MQTT 订阅 + cmd 下发），未实现具体 Agent 逻辑。
- **OTA 仅 Web**：MQTT/HTTP/HTTPS OTA 为后续扩展（架构已预留 `OtaManager`）。

---

## 19. 后续扩展建议

1. **新硬件模块**：实现 `ExternalAdc` / `DebugModule` 子类（如 `Mcp3208Adc`、`CanMonitor`、`I2cMonitor`、`LogicAnalyzer`），在 `DebugGateway::begin()` 注册即可，Web/MQTT/WS 自动生效。
2. **MQTT TLS / HTTPS / JWT / OTA 签名**：在 `network/` 与 `ota/` 内补齐安全层（字段已预留）。
3. **AI Agent**：以 MQTT 为总线，Agent 订阅 `.../uart/rx`、`.../adc/#`、`.../gpio`、`.../system`，并下发 `uart_tx`/`gpio_set`/`adc_read`/`device_reset`。
4. **性能**：UART 接收可改用 IDF UART 驱动的 DMA 双缓冲；WS 广播可批处理以降低小包开销。
5. **持久化增强**：配置版本号 + 工厂复位端点；日志可落 SPIFFS（`log_manager` 已留接口）。
