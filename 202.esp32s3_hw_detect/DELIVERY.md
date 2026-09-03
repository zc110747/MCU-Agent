# ESP32-S3 Remote Hardware Debugger — 交付文档

> 框架目标：把 ESP32-S3 变成网络化硬件调试网关，实时把目标 MCU 的 **UART 日志 / 外部 ADC 电压 / GPIO 状态** 通过 **Web(WebSocket) + MQTT** 提供给浏览器、上位机与未来 AI Agent。
> 架构核心：**统一 Event Bus（FreeRTOS Queue）** 解耦生产者（UART/ADC/GPIO）与消费者（WebSocket/MQTT/Log），新增模块（CAN/I2C/SPI/逻辑分析仪…）零改动核心。

---

## 1. 完整工程目录

```text
202.esp32s3_hw_detect/
├── 202.esp32s3_hw_detect.ino  # 入口（文件名须与工程目录同名）；强制 #include 各模块 .cpp 形成单编译单元
├── build_oneclick.bat         # 一键编译（检查 arduino-cli + 装库 + compile）
├── flash-esp32.bat            # 一键下载/烧录（参考 201 项目，支持自动扫描 COM + monitor）
├── DELIVERY.md                # 本文档
├── .vscode/
│   ├── settings.json          # Arduino CLI 关联设置
│   ├── tasks.json             # "Build (arduino-cli)" 默认构建任务
│   └── launch.json            # 3 个 Cortex-Debug 配置（内置 JTAG / ESP-Prog / FTDI）
└── app/                       # 注意：下划线模块目录，Arduino 不会自动编译其中 .cpp，靠 .ino 强制 include
    ├── debug_gateway.h/.cpp   # 总编排：启动流程 + EventTask 分发 + 命令解析
    ├── debug_module.h         # DebugEvent / DebugEventType / DebugModule 扩展接口
    ├── event_bus.h/.cpp        # FreeRTOS Queue 事件总线（非阻塞 push）
    ├── ring_buffer.h           # 单生产者单消费者字节环形缓冲（UART 抗突发）
    └── pin_manager.h/.cpp      # 运行时 GPIO 资源强制管理（禁用 19/20/48 与重复占用）
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
├── bsp/
│   ├── uart_monitor.h/.cpp     # UART 监视（HardwareSerial + 任务 + 二进制/文本）
│   ├── adc_monitor.h/.cpp      # ExternalAdc 抽象 + ADS1115 实现 + 采样任务
│   └── gpio_monitor.h/.cpp     # GPIO 数字输入/输出
├── storage/
│   ├── config_manager.h/.cpp   # Preferences(NVS) 持久化
│   └── log_manager.h/.cpp       # 分级日志 + 环形缓冲 + 可选 WS 转发
└── ota/
    └── ota_manager.h/.cpp      # Web OTA（仅写 app 分区，保护 NVS）
```

> 说明：Arduino 构建模型下，**根目录 `.ino` 须与工程目录同名**（否则 arduino-cli 报 `main file missing`）。子目录的 `.cpp` 不会被自动编译，工程通过 `.ino` 中统一 `#include "xxx.cpp"` 把所有模块拼成**单一编译单元**（与 201 项目一致的范式）。所有头文件使用传统 `#ifndef`/`#define`/`#endif` 守卫（arduino-cli 会把工程复制到 `.build/sketch/` 用不同路径解析，`#pragma once` 会失效导致重定义）。

---

## 2. 关键源码说明（按需查阅，见各 .cpp）

- **EventBus**（`app/event_bus.*`）：生产者 `push()` 用 `xQueueSend(...,0)` **永不阻塞**；队列满则丢弃并 `overflow++`，保证慢网络客户端不会拖垮 UART 接收。
- **UART Monitor**（`bsp/uart_monitor.*`）：`HardwareSerial(1)` 默认 115200/8N1；RX→环形缓冲→按行（文本）或按块（HEX）成帧→`DebugEvent` 入总线；`baud/data/stop/parity` 可运行时改。
- **ADC Monitor**（`bsp/adc_monitor.*`）：抽象层 `ExternalAdc`，首版实现 `Ads1115Adc`（I2C，单拍 860 SPS）。核心只依赖接口，换芯片只写新子类。
- **GPIO Monitor**（`bsp/gpio_monitor.*`）：4 路监控，输入变化即推事件；输出经 PinManager 校验。
- **网关**（`app/debug_gateway.*`）：`begin()` 按 34 节顺序启动；`EventTask` 单消费者把事件扇出到 WS/MQTT，并每 5s 广播 SYSTEM 状态。
- **命令分发**：MQTT `cmd` 主题与 WebSocket 入站 JSON 都汇聚到 `getGateway().handleJsonCommand()`，单点校验。

---

## 3. 构建环境（Arduino CLI + VS Code）

本工程已**从 PlatformIO 迁移到 Arduino CLI + VS Code**。核心要点：

- **板卡（FQBN）**：`esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600`
  （N16R8，QSPI PSRAM 16MB，Flash 16MB，默认分区含 `ota_0/ota_1`）
- **ESP32 Arduino Core**：`3.3.11`（已装于 `D:\software\arduino-cli\data\packages\esp32`）
- **arduino-cli**：`1.5.2-rc.1`（或 PATH 上的任意 `arduino-cli`）
- **构建命令**（与 `build_oneclick.bat` 一致）：
  ```bat
  arduino-cli compile -j 8 ^
    -b "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600" ^
    --build-path ".build" .
  ```

> 经验：PlatformIO 在本机被 `genie-trash` 守护锁定 `.platformio` 目录，导致 `packages.lock` 权限失败与框架解包死循环；Arduino CLI 使用独立未受保护的 `D:\software\arduino-cli\data`，编译干净通过，故迁移。

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
| ArduinoJson | 7.4.3 | 所有 JSON（MQTT/WS/REST） | 否（`arduino-cli lib install`） |
| PubSubClient | 2.8.0 | MQTT 客户端 | 否（已装） |
| WebSockets | 2.7.2 | WebSocket Server | 否（已装） |
| WiFi / WebServer / Preferences / HardwareSerial / Wire / Update / freertos | — | 网络/存储/串口/I2C/OTA/RTOS | **Arduino Core 自带** |

> 三个外部库通过 `arduino-cli lib install ArduinoJson PubSubClient WebSockets` 装入 `D:\software\arduino-cli\sketchbook\libraries`（`build_oneclick.bat` 首跑自动执行，幂等）。

> 刻意不引入 ESPAsyncWebServer、ADS1X15 等库：Web 用核心 `WebServer` + `WebSocketsServer`；ADC 自写 I2C 驱动（仅 ~40 行），降低依赖与 Flash 占用。

---

## 6. 编译方法

**方式 A — 一键脚本（推荐）**：双击 `build_oneclick.bat`，脚本会自动检查 `arduino-cli`、幂等安装三个外部库、执行 `arduino-cli compile`，失败时暂停并输出英文日志。

**方式 B — VS Code 任务**：`Ctrl+Shift+B` → 选 `Build (arduino-cli)`（来自 `.vscode/tasks.json`），等价于方式 A 的编译命令，产物输出到 `.build/`。

**方式 C — 手动命令**：
```bat
cd D:\user_project\git\MCU-Agent\202.esp32s3_hw_detect
arduino-cli lib install ArduinoJson PubSubClient WebSockets   :: 仅首次/缺库
arduino-cli compile -j 8 ^
  -b "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600" ^
  --build-path ".build" .
```

编译产物位于 `.build/`：`202.esp32s3_hw_detect.ino.bin` / `.elf` / `.merged.bin`（16MB）/ `partitions.bin` / `bootloader.bin`。

> 首跑若 arduino-cli 未装对应核心，会自动下载 ESP32 工具链 + Arduino 框架（约数百 MB，视网速数分钟）；本机核心 3.3.11 已就绪，无需重复下载。

---

## 7. 烧录方法

- **一键脚本（推荐）**：双击 `flash-esp32.bat`，脚本会查找 `arduino-cli`（PATH → 本地 → `D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit`），若未传端口则通过 pyserial 自动扫描可用 `COMx`，然后 `arduino-cli upload`。
  ```bat
  flash-esp32.bat                 :: 自动扫描 COM 并烧录
  flash-esp32.bat COM7            :: 指定端口烧录
  flash-esp32.bat COM7 monitor    :: 烧录后自动打开串口监视（115200）
  flash-esp32.bat --no-pause      :: 非交互模式，结束不暂停（适合 CI）
  ```
  烧录后脚本额外给出一次 DTR/RTS 复位脉冲以确保设备进入用户程序。
- 手动命令：`arduino-cli upload -p COM7 -b "<FQBN>" --build-path ".build" .`
- 烧录后设备以 **USB CDC（GPIO19/20）** 输出日志，波特 115200；也可用 VS Code Cortex-Debug（`.vscode/launch.json`）通过内置 JTAG / ESP-Prog / FTDI 进行硬件调试（elf 路径指向 `.build/202.esp32s3_hw_detect.ino.elf`）。

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
{ "type":"led",       "gpio":48, "mode":4, "mode_str":"cycle", "on":1, "r":0, "g":255, "b":0,
  "brightness":40, "timestamp":... }
{ "type":"pwm",       "active":1, "pin":21, "period":1000, "duty":25, "freq":1000,
  "resolution":12, "timestamp":... }
{ "type":"log",       "line":"[123][INFO][UART] ..." }
{ "type":"system",    "device":..., "uptime":..., "free_heap":..., ... }
{ "type":"state",     "ts":..., "gpio":[...], "led":{...}, "pwm":{...}, "adc":[...], "adc_src":"..." }
```
> 禁止浏览器高频 HTTP polling 拉 UART；全部走 WebSocket 实时推送。

### 11.1 `state` 状态快照（服务器主动推送，400 ms/帧）

Interface 页的四项实时读数（GPIO / WS2812 / PWM / ADC）统一由 `state` 快照驱动。
ws_task 每 `AppConfig::STATE_PUSH_INTERVAL_MS = 400` ms 检查一次，**仅当存在已连接客户端时**
（`_clients > 0`）构造并广播；无浏览器在线时完全零开销。

该帧**刻意不进 EventBus 队列**：若把 `state` 做成 `DebugEvent`，其 `sizeof` 将从约 220 B 膨胀到
800 B+，深度 64 的事件队列会多占约 37 KB DRAM。改为在 ws_task 内直接读模块缓存并 `broadcastTXT()`，
DRAM 仅增约 276 B。

```json
{
  "type": "state",
  "ts": 12345678,
  "gpio": [
    { "pin": 4, "state": 1, "dir": 1 },
    { "pin": 5, "state": 0, "dir": 0 },
    { "pin": 6, "state": 1, "dir": 0 },
    { "pin": 7, "state": 1, "dir": 0 }
  ],
  "led": { "pin": 48, "mode": 4, "mode_str": "cycle", "on": 1, "r": 0, "g": 255, "b": 0,
           "brightness": 40, "ready": 1 },
  "pwm": { "active": 1, "pin": 21, "period": 1000, "duty": 25, "freq": 1000, "resolution": 12 },
  "adc": [
    { "ch": 0, "raw": 2048, "voltage": 3.300 },
    { "ch": 1, "raw": 0,    "voltage": 0.000 },
    { "ch": 2, "raw": 0,    "voltage": 0.000 },
    { "ch": 3, "raw": 0,    "voltage": 0.000 }
  ],
  "adc_src": "internal-adc1",
  "adc_ready": 1
}
```

| 字段 | 语义 / 保证 |
|------|-------------|
| `gpio[].state` | `digitalRead()` 的真实回读电平（0/1）。`gpio_monitor.setPin()` 写后必回读再发布，界面不会"下发即回显" |
| `gpio[].dir`   | 1 = 输出，0 = 输入上拉（NVS 持久化） |
| `led.on/r/g/b` | 灯珠**此刻**实际输出的颜色；闪烁/循环模式下会随半周期（250 ms）跳变，`on=0` 为灭半周期或已关闭 |
| `pwm.freq`     | LEDC 换算后的实际频率（Hz），非用户输入回显 |
| `pwm.resolution` | 实际分辨率 8..12 bit（按 `80MHz / 2^res >= freq` 自动降档） |
| `adc[].voltage` | 已代入量程 / 分压比 / 偏移换算后的电压（V），取 `AdcMonitor::latest()` 缓存，**不额外占用 I2C** |
| `adc_src`      | `ads1115` 或 `internal-adc1`（ADS1115 探测失败自动回退） |

**连接语义**：`onConnect()` 中 `_clients++` 并把 `_lastState = 0`，使新页面连上后**立即**收到首帧快照
（不必等到下一个 400 ms 周期）；`onDisconnect()` 递减计数，计数归零后停止构造快照。

**首屏兼容**：`/api/gpio` GET 同步返回 `led_state`（mode/mode_str/on/r/g/b）、`pwm.freq` 与内联 `adc[]`
+ `adc_src`，使 WebSocket 尚未连上时首屏也能显示真实状态。

**前端渲染**：单页 JS 收到 `state` 后 `applyState(m)` 分发到 `updateGpio/updateLed/updatePwm/updateAdc`。
页面已移除全部 `setInterval` 轮询与断线 `location.reload()`，改为 3 s 自动重连 WebSocket。
WS2812 卡片同时显示模式与实时输出（如 `RGB CYCLE | ON rgb(0,0,255)`），选中态仅用中性描边
（全站单色，无彩色高亮）。PWM 卡片把设备回传参数回填输入框，但**聚焦中的输入框不覆盖**。
ADC 曲线按峰值自适应缩放（`scale = peak × 1.15`）并标注 `peak x.xx V`。

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

## 16. 内存占用（arduino-cli 实测）

```text
Sketch uses 1017991 bytes (77%) of program storage space. Maximum is 1310720 bytes.
Global variables use 80436 bytes (24%) of dynamic memory, leaving 247244 bytes for local variables. Maximum is 327680 bytes.
```

| 项 | 占用 | 上限 | 占比 |
|----|------|------|------|
| Flash（程序） | 1,017,991 B | 1,310,720 B | 77% |
| DRAM（全局变量） | 80,436 B | 327,680 B | 24% |

> PSRAM 16MB 由 `PSRAM=opi` 启用，供大缓冲（如 WS 帧、日志环形缓冲）分配；上述 DRAM 24% 已为学校内变量，余量充足。运行时 `free_heap` 由 SYS 状态 JSON / WebSocket `system` 推送（见 §10/§11）。

---

## 17. 已完成测试

- **编译**：`arduino-cli compile` 退出码 0，零错误零警告（见 §16 内存实测）。
- **构建范式验证**：Arduino 单编译单元（`*.ino` 强制 `#include` 各子目录 `.cpp`）+ 全部 19 个头文件 `#ifndef` 守卫，成功规避 arduino-cli 复制工程后的头文件重定义。
- **静态校验**：所有 `#include` 缺失已补（ArduinoJson / WiFi / esp_mac / pin_config 等）；`constexpr IPAddress`、`Update.write` const 转换、回调静态成员可见性等编译错误已修复。
- **脚本就绪**：`build_oneclick.bat`、`flash-esp32.bat` 已生成并验证可调用（参考 201 项目范式）。
- **待真机验证**（需烧录后在本机串口 + 浏览器执行）：UART/ADC/GPIO 端到端数据流、WiFi STA+AP、MQTT 发布订阅、WebSocket 实时推送、Web OTA 升级。架构与协议已就绪，功能验证不阻塞交付。

> 回归验收可扩展 `verify_*.py`（串口/网络自测）给出 pass/fail 计数，沿用 201 项目节奏。

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

---

## 20. 连接与接线指南（真机上手）

### 20.1 软件接入（PC ↔ 设备）

首次上电（NVS 未存 WiFi 账号）设备**默认进入 AP 热点模式**：

| 项 | 值 |
|---|---|
| 热点名 | `wifi-XXXX`（XXXX = MAC 末 4 位十六进制大写） |
| 热点密码 | `debugger123` |
| 设备 IP | `192.168.4.1`（Web:80 / WebSocket:81） |
| 运行指示 | GPIO48 板载 RUN LED 闪烁 = 正常 |
| Web 鉴权 | Basic，用户名 `admin` / 密码 `admin`（默认，可在 Dashboard 改） |

**方式一 · 连设备自带热点（最快）**：USB 线保持供电 → 等 3–5 s 启动 → PC 连热点 → 浏览器开 `http://192.168.4.1`（仪表盘自动连 `ws://192.168.4.1:81` 实时推送）。串口助手开烧录口（如 COM22）@115200 可见启动日志与 AP 名。

**方式二 · 接入局域网（STA）**：在仪表盘 WiFi 配置区填路由器 SSID/密码 → Save → 设备重启切 STA。成功后**自带热点消失**，从串口日志或路由器 DHCP 列表取 STA IP，浏览器开 `<STA_IP>:80`。STA 连不上会自动回退 AP。

### 20.2 接目标 MCU（硬件调试接线）

| 功能 | ESP32-S3 引脚 | 接目标 | 备注 |
|---|---|---|---|
| UART 监视 | GPIO17 (RX) | 目标 MCU **TX** | 默认 115200/8N1，Dashboard 可改 |
| | GPIO18 (TX) | 目标 MCU **RX** | |
| 外部 ADC | GPIO8 (SDA) / GPIO9 (SCL) | ADS1115 | I²C 地址 `0x48`（ADDR→GND），400 kHz |
| GPIO 监视 | GPIO4 / 5 / 6 / 7 | 被测数字信号 | 默认输入上拉，可切输出 |
| 禁止占用 | 19 / 20 / 48 | — | USB D-/D+ 与 RUN LED，PinManager 锁死 |

### 20.3 MQTT 接入（可选，接消息总线 / AI Agent）

- 默认 broker `192.168.10.1:1883`，Dashboard MQTT 区可改。
- Topic 前缀 `remote-debugger/<device_id>/...`，`device_id = esp32s3-AABBCCDD`（板载 MAC）。
- 例：UART 接收流 `remote-debugger/esp32s3-AABBCCDD/uart/rx`；命令下发 `.../cmd`。

---

## 21. 真机验收（tools/verify/*.py）

纯 stdlib、英文输出（GBK 安全）、pass/fail 计数；设备 IP 用参数或环境变量 `RHD_IP` 指定（默认 `192.168.4.1`）。

| 脚本 | 验证点 |
|---|---|
| `verify_web.py` | Dashboard 200、`/api/status` JSON 字段、无凭证 401 鉴权、`/api/wifi`、`/api/logs` |
| `verify_gpio.py` | 4 路监视脚 [4,5,6,7]、GPIO4 写 1/写 0 回读、非监视脚 13 与保留脚 48 被 400 拒绝 |
| `verify_adc.py` | `/api/adc/read` 4 通道 raw+voltage、`/api/adc/config` fsr；ADS1115 未接时 WARN 不 FAIL |
| `verify_ws.py` | WS `:81` 握手 101、持续接收带 type 的 JSON（adc/system/log）、**`state` 快照可观测且必须同时含 gpio/led/pwm/adc 四段**、心跳观测 |
| `verify_interface.py` | **Interface 四项实时性端到端**：经 WS 下发命令后回读 `state` 断言——GPIO SET→`state=1` 且 `dir=1` / CLEAR→`state=0`；`ws2812_set cycle` 后 2 s 内 `led` 的 `(on,r,g,b)` 出现 ≥2 种组合（证明是实时输出而非模式回显）、`off` 后 `on=0`；`pwm_set` 的 `period/duty` 被镜像且 stop 后 `active=0`；`adc[]` 为 4 通道且 `voltage` 为数值。测试结束恢复原 LED 模式 |
| `run_all.py` | 汇总执行以上 5 项，输出 `TOTAL: n/5 scripts passed`，退出码 0/1 |

```bat
cd tools\verify
python run_all.py                :: AP 模式（连上设备热点后跑）
python run_all.py 192.168.1.50   :: STA 模式（设备已入局域网）
```

注意：`verify_gpio.py` 会把 GPIO4 切为输出并写入 NVS 方向掩码（测完保持输出低电平，属预期）。UART 双向流（`uart_tx` 下发）与 MQTT 发布订阅需接目标板/broker 后按 §10/§11 协议手工或脚本扩展验证。
