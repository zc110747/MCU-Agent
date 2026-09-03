# ESP32-S3 Remote Hardware Debugger 调试网关开发任务

你是一名资深 ESP32-S3 / Arduino / FreeRTOS / 网络协议 / 嵌入式调试工具开发工程师。

请从零实现一个完整、可编译、可烧录、可实际运行的：

# ESP32-S3 Remote Hardware Debugger

目标是将 ESP32-S3 开发板作为一个**网络化硬件调试网关**，通过 WiFi 将目标 MCU 的 UART 日志、外部 ADC 电压、GPIO 状态等硬件调试信息实时提供给：

1. Web 浏览器
2. MQTT 上位机
3. 后续 AI Agent / 自动化测试系统

本项目必须使用：

* ESP32-S3
* Arduino Core for ESP32
* Arduino Framework
* FreeRTOS（使用 ESP32 Arduino 自带 FreeRTOS）
* C++
* Arduino CLI编译
* 不使用 ESP-IDF Application Framework 直接开发业务逻辑
* 可以使用 ESP32 Arduino Core 底层提供的 FreeRTOS、WiFi、UART 等能力

---

# 一、硬件约束

目标芯片：

ESP32-S3。

以下 GPIO 严禁作为本项目普通 GPIO 使用：

```text
GPIO19
GPIO20
GPIO48
```

原因：

```text
GPIO19/GPIO20
USB D-/D+

GPIO48
保留给板载硬件/后续专用功能
```

因此整个工程必须建立统一的 GPIO 资源管理机制。

禁止在业务代码中随意硬编码：

```cpp
digitalWrite(19, ...);
digitalWrite(20, ...);
digitalWrite(48, ...);
```

也禁止 ADC/UART/GPIO 模块间重复占用同一个 GPIO。

创建：

```text
config/pin_config.h
```

统一管理所有引脚。

例如：

```cpp
namespace DebugPins {

constexpr int RESERVED_USB_DM = 19;
constexpr int RESERVED_USB_DP = 20;
constexpr int RESERVED_GPIO48 = 48;

// UART Debug Port
constexpr int UART_RX = ...;
constexpr int UART_TX = ...;

// ADC interface
constexpr int ADC_I2C_SDA = ...;
constexpr int ADC_I2C_SCL = ...;

// Optional GPIO monitor
constexpr int GPIO_MONITOR_0 = ...;
}

```

具体 GPIO 可以根据 ESP32-S3 N16R8 常规可用 GPIO 选择，但必须：

1. 避开 GPIO19/20/48
2. 避开 ESP32-S3 启动相关风险 GPIO
3. 避免与板载 USB/Flash/PSRAM 冲突
4. 启动时打印最终 GPIO 配置
5. 所有 GPIO 配置集中管理

如果无法确认某个 GPIO 是否安全，不允许猜测，必须在代码中留下明确配置项。

---

# 二、项目总体目标

系统启动后：

```text
ESP32-S3
    │
    ├── WiFi
    │
    ├── Web Server
    │       └── WebSocket
    │
    ├── MQTT Client
    │
    ├── UART Debug Monitor
    │
    ├── External ADC Monitor
    │
    ├── GPIO Monitor
    │
    ├── Event Bus
    │
    ├── Ring Buffer
    │
    └── OTA
```

最终形成：

```text
                 ┌─────────────────────┐
                 │ PC / Browser        │
                 │ Web Dashboard       │
                 └──────────┬──────────┘
                            │ WebSocket
                            │
                 ┌──────────▼──────────┐
                 │                     │
                 │      ESP32-S3       │
                 │ Remote Debugger     │
                 │                     │
                 └──────────┬──────────┘
                            │
             ┌──────────────┼──────────────┐
             │              │              │
             ▼              ▼              ▼
           UART            ADC            GPIO
             │              │              │
             └──────────────┼──────────────┘
                            │
                            ▼
                       Target MCU
```

同时：

```text
ESP32-S3
    │
    └── MQTT
          │
          ▼
     PC / Server / AI Agent
```

---

# 三、核心设计原则

必须遵守以下原则：

## 1. 模块化

禁止所有代码写入：

```text
main.cpp
```

必须按照功能拆分。

推荐：

```text
src/
├── main.cpp
│
├── app/
│   ├── debug_gateway.cpp
│   ├── debug_gateway.h
│   ├── event_bus.cpp
│   └── event_bus.h
│
├── network/
│   ├── wifi_manager.cpp
│   ├── wifi_manager.h
│   ├── mqtt_manager.cpp
│   ├── mqtt_manager.h
│   ├── web_server.cpp
│   ├── web_server.h
│   ├── websocket_manager.cpp
│   └── websocket_manager.h
│
├── bsp/
│   ├── uart_monitor.cpp
│   ├── uart_monitor.h
│   ├── adc_monitor.cpp
│   ├── adc_monitor.h
│   ├── gpio_monitor.cpp
│   └── gpio_monitor.h
│
├── storage/
│   ├── config_manager.cpp
│   ├── config_manager.h
│   ├── log_manager.cpp
│   └── log_manager.h
│
├── ota/
│   ├── ota_manager.cpp
│   └── ota_manager.h
│
└── config/
    ├── pin_config.h
    ├── app_config.h
    └── mqtt_config.h
```

---

# 四、统一 Event Bus

所有调试数据必须首先进入统一事件系统。

定义：

```cpp
enum class DebugEventType {
    UART_RX,
    UART_TX,
    ADC_SAMPLE,
    GPIO_STATE,
    SYSTEM,
    ERROR
};
```

定义：

```cpp
struct DebugEvent {
    DebugEventType type;
    uint32_t timestamp;
    uint8_t channel;
    uint16_t length;
    uint8_t data[256];
};
```

可以根据实际 RAM 优化结构。

必须使用：

```text
FreeRTOS Queue
```

或者：

```text
Queue + RingBuffer
```

实现线程安全的数据传输。

禁止：

```text
UART Task
   ↓
直接调用 Web API

ADC Task
   ↓
直接调用 MQTT

GPIO Task
   ↓
直接操作 Web
```

必须：

```text
UART
 ADC
 GPIO
  │
  ▼
Event Bus
  │
  ├── WebSocket
  ├── MQTT
  ├── Log
  └── Future AI Agent
```

这样以后增加 CAN、SPI、I2C、逻辑分析仪等功能时，不需要修改核心架构。

---

# 五、UART Remote Debug Monitor

实现一个独立 UART Monitor。

使用：

```cpp
HardwareSerial
```

默认参数：

```text
115200
8N1
```

但必须支持通过 Web/MQTT 修改：

```text
baudrate
data bits
stop bits
parity
```

至少支持：

```text
9600
19200
38400
57600
115200
230400
460800
921600
```

UART：

```text
ESP32-S3 RX ← Target MCU TX
ESP32-S3 TX → Target MCU RX
```

UART RX 数据进入：

```text
UART Task
   ↓
RingBuffer
   ↓
Event Bus
```

必须支持：

* 二进制数据
* ASCII 数据
* UTF-8 日志
* 大量连续日志
* 行结束符检测

不能假设 UART 数据一定是字符串。

---

# 六、UART Web 实时显示

Web 页面显示：

```text
Remote UART Monitor
```

例如：

```text
14:31:01.123  Booting...
14:31:01.145  System Init
14:31:01.211  WiFi Connected
14:31:02.003  ADC initialized
14:31:02.551  ERROR: Sensor timeout
```

支持：

```text
暂停
继续
清空
自动滚动
搜索
导出日志
```

WebSocket 实时推送。

禁止通过浏览器高频 HTTP polling 获取 UART。

---

# 七、UART Remote TX

Web 页面增加：

```text
UART TX
```

例如输入：

```text
AT+RST
```

点击：

```text
Send
```

ESP32-S3：

```text
Web
 ↓
WebSocket
 ↓
UART Manager
 ↓
Target MCU
```

MQTT 同样支持 UART TX。

---

# 八、External ADC

第一版必须将 ADC 设计为独立抽象层。

不要直接把 ADC 芯片型号写死在核心框架中。

定义：

```cpp
class ExternalAdc {
public:
    virtual bool begin() = 0;
    virtual bool readChannel(uint8_t channel,
                             uint32_t& raw,
                             float& voltage) = 0;
};
```

后续可以增加：

```text
ADS1115
ADS1015
MCP3008
MCP3208
其它 ADC
```

核心系统不应该关心具体 ADC 型号。

---

# 九、ADC 第一版实现

第一版选择一个常见外部 ADC，例如：

```text
ADS1115
```

通过 I2C：

```text
SDA
SCL
```

连接。

至少支持：

```text
CH0
CH1
CH2
CH3
```

Web显示：

```text
ADC Channels

CH0    3.301 V
CH1    1.802 V
CH2    0.000 V
CH3    5.012 V
```

注意：

如果被测电压超过 ADC 输入范围，必须通过外部分压电路。

软件必须允许配置：

```cpp
gain
divider_ratio
offset
calibration
```

例如：

```text
实际电压
=
ADC电压 × 分压比例 + Offset
```

---

# 十、ADC采样任务

创建：

```text
ADC Task
```

默认：

```text
100 ms
```

即：

```text
10 Hz
```

支持配置：

```text
1Hz
5Hz
10Hz
20Hz
50Hz
100Hz
```

每次采样：

```text
raw
voltage
timestamp
channel
```

生成：

```text
ADC_SAMPLE
```

事件。

---

# 十一、GPIO Monitor

预留 GPIO Monitor 模块。

支持：

```text
Digital Input
Digital Output
```

Web显示：

```text
GPIO Monitor

GPIO4   HIGH
GPIO5   LOW
GPIO6   HIGH
GPIO7   LOW
```

后续支持：

```text
GPIO SET
GPIO CLEAR
GPIO TOGGLE
```

GPIO 必须经过统一 Pin Manager 检查。

禁止使用：

```text
GPIO19
GPIO20
GPIO48
```

---

# 十二、MQTT

实现 MQTT Client。

MQTT 配置支持：

```text
Broker
Port
Username
Password
Client ID
Keep Alive
TLS enable
```

默认 Topic：

```text
remote-debugger/<device_id>/
```

例如：

```text
remote-debugger/esp32s3-001/status
remote-debugger/esp32s3-001/uart/rx
remote-debugger/esp32s3-001/uart/tx
remote-debugger/esp32s3-001/adc/ch0
remote-debugger/esp32s3-001/adc/ch1
remote-debugger/esp32s3-001/gpio
remote-debugger/esp32s3-001/event
remote-debugger/esp32s3-001/cmd
```

---

# 十三、MQTT Payload

统一 JSON 格式。

UART：

```json
{
    "timestamp": 12345678,
    "channel": 0,
    "encoding": "text",
    "data": "System Init"
}
```

ADC：

```json
{
    "timestamp": 12345678,
    "channel": 0,
    "raw": 1234,
    "voltage": 3.301
}
```

GPIO：

```json
{
    "timestamp": 12345678,
    "gpio": 4,
    "state": 1
}
```

系统：

```json
{
    "device": "esp32s3-001",
    "uptime": 123456,
    "free_heap": 123456,
    "wifi_rssi": -48,
    "firmware": "1.0.0"
}
```

---

# 十四、MQTT Remote Command

支持：

```text
remote-debugger/<device_id>/cmd
```

命令：

```json
{
    "cmd": "uart_config",
    "baud": 115200
}
```

```json
{
    "cmd": "uart_tx",
    "data": "AT\r\n"
}
```

```json
{
    "cmd": "gpio_set",
    "gpio": 4,
    "value": 1
}
```

```json
{
    "cmd": "gpio_set",
    "gpio": 4,
    "value": 0
}
```

```json
{
    "cmd": "adc_read"
}
```

所有 MQTT 命令必须：

1. 参数校验
2. GPIO 合法性检查
3. 权限/安全检查
4. 执行结果返回

---

# 十五、Web Server

ESP32-S3 内置 Web Server。

页面包括：

```text
Dashboard
UART
ADC
GPIO
MQTT
WiFi
System
OTA
Logs
```

首页：

```text
Remote Hardware Debugger
```

显示：

```text
Device ID
Firmware
Uptime
Free Heap
WiFi RSSI
IP Address
MQTT Status
UART Status
ADC Status
```

---

# 十六、WebSocket

WebSocket 用于：

```text
UART实时日志
ADC实时数据
GPIO实时状态
系统事件
```

WebSocket 消息统一：

```json
{
    "type": "uart",
    "timestamp": 12345678,
    "data": "hello"
}
```

ADC：

```json
{
    "type": "adc",
    "channel": 0,
    "voltage": 3.301,
    "timestamp": 12345678
}
```

GPIO：

```json
{
    "type": "gpio",
    "gpio": 4,
    "state": 1,
    "timestamp": 12345678
}
```

---

# 十七、配置管理

配置必须统一保存。

推荐使用：

```text
Preferences / NVS
```

配置：

```text
WiFi SSID
WiFi Password
MQTT Broker
MQTT Port
MQTT Username
MQTT Password
Device Name
UART Baudrate
ADC configuration
GPIO configuration
```

不能每次启动都要求重新配置。

---

# 十八、WiFi

支持：

```text
STA Mode
AP Mode
```

建议：

```text
正常模式：
STA

配置失败：
AP fallback
```

AP：

```text
wifi-XXXX（XXXX = MAC 末 4 位十六进制大写，如 wifi-A118）
```

Web：

```text
192.168.4.1
```

通过 Web 配置 WiFi。

---

# 十九、日志系统

建立统一日志：

```text
LOG_DEBUG
LOG_INFO
LOG_WARN
LOG_ERROR
```

输出：

```text
Serial
```

同时可以选择：

```text
Web
MQTT
Flash
```

日志必须包含：

```text
timestamp
level
module
message
```

例如：

```text
[123456][INFO][UART] UART initialized
[123500][INFO][MQTT] Connected
[123510][WARN][ADC] CH2 voltage high
```

---

# 二十、FreeRTOS任务

使用 Arduino Core 自带 FreeRTOS。

建议任务：

```text
NetworkTask
MQTTTask
WebTask
UARTTask
ADCTask
GPIOTask
EventTask
LogTask
```

但不要为了形式创建过多任务。

必须考虑：

```text
stack size
priority
core affinity
queue size
heap usage
```

所有任务创建后输出：

```text
task name
stack
priority
core
```

---

# 二十一、线程安全

必须保证：

```text
UART
MQTT
WebSocket
ADC
GPIO
Logger
```

之间线程安全。

使用：

```text
FreeRTOS Queue
Semaphore
Mutex
RingBuffer
```

禁止多个任务同时直接操作共享 String / buffer。

尤其注意：

```text
MQTT callback
WebSocket callback
UART task
```

之间的数据生命周期。

禁止产生：

```text
use-after-free
buffer overflow
race condition
deadlock
```

---

# 二十二、性能要求

系统必须能够持续接收：

```text
UART 115200 baud
```

而不丢数据。

目标：

```text
115200 baud
连续日志运行30分钟
无明显丢包
```

如果 UART 数据量过大：

```text
RingBuffer
→ Event Queue
→ WebSocket/MQTT
```

必须具备：

```text
overflow counter
drop counter
```

Web/MQTT发送速度不足时不能阻塞 UART 接收。

---

# 二十三、断线处理

WiFi断开：

```text
自动重连
```

MQTT断开：

```text
自动重连
```

WebSocket客户端断开：

```text
释放资源
```

MQTT不可用时：

```text
UART/ADC继续正常工作
```

不能因为 MQTT 服务器关闭导致整个 Debugger 停止。

同理：

```text
Web客户端关闭
```

不能影响 UART。

---

# 二十四、OTA

预留 OTA 模块。

第一阶段实现：

```text
Web OTA
```

Web：

```text
Firmware Upload
```

后续支持：

```text
MQTT OTA
HTTP OTA
HTTPS OTA
```

OTA不能破坏：

```text
NVS配置
WiFi配置
MQTT配置
设备ID
```

OTA失败必须能够正常回滚/重新启动。

---

# 二十五、安全设计

第一阶段至少实现：

```text
Web 登录密码
MQTT Username/Password
OTA 校验
```

禁止把：

```text
MQTT password
WiFi password
```

直接打印到日志。

后续预留：

```text
HTTPS
MQTT TLS
JWT
OTA signature
```

不要为了第一版而引入复杂 Secure Boot。

本项目当前重点是：

```text
功能正确
架构正确
可扩展
```

---

# 二十六、设备唯一 ID

设备启动时生成：

```text
ESP32-S3 MAC
```

构造：

```text
esp32s3-XXXXXXXX
```

例如：

```text
esp32s3-A1B2C3D4
```

作为：

```text
MQTT Client ID
MQTT Topic
Web显示
```

---

# 二十七、未来扩展接口

架构必须预留：

```text
UART
ADC
GPIO
I2C
SPI
CAN
RS485
USB
JTAG
SWD
Logic Analyzer
Temperature
Power Monitor
```

统一：

```cpp
class DebugModule {
public:
    virtual bool begin() = 0;
    virtual void update() = 0;
    virtual const char* name() = 0;
};
```

未来可以增加：

```text
CanMonitor
I2cMonitor
SpiMonitor
PowerMonitor
LogicAnalyzer
```

而无需修改：

```text
MQTT
Web
EventBus
```

核心架构。

---

# 二十八、AI Agent接口

虽然第一版不实现 AI，但必须预留：

```text
AI Agent API
```

AI Agent以后能够通过 MQTT 获取：

```text
UART
ADC
GPIO
System
Events
Errors
```

并可以执行：

```text
UART TX
GPIO SET
GPIO RESET
ADC READ
DEVICE RESET
```

最终形成：

```text
AI Agent
    │
    │ MQTT
    ▼
ESP32-S3 Remote Debugger
    │
    ├── UART
    ├── ADC
    ├── GPIO
    ├── I2C
    ├── SPI
    └── CAN
        │
        ▼
    Target MCU
```

---

# 二十九、Web UI设计要求

不要使用复杂前端框架。

第一版优先：

```text
HTML
CSS
JavaScript
WebSocket
```

页面必须可以直接由 ESP32-S3 提供。

尽量减少：

```text
JS bundle
CSS bundle
```

以降低 Flash 占用。

UI至少包括：

```text
Dashboard
UART Console
ADC Monitor
GPIO Monitor
MQTT Status
System Information
Configuration
OTA
```

ADC最好提供简单实时曲线。

UART支持：

```text
文本模式
Hex模式
自动滚动
暂停
清空
过滤
```

---

# 三十、工程构建要求

必须提供：

```text
platformio.ini
```

同时保证能够迁移到：

```text
Arduino CLI
```

指定：

```text
ESP32-S3
```

使用 Arduino Core。

编译时不得依赖：

```text
Windows绝对路径
开发者个人路径
本机特殊环境
```

---

# 三十一、依赖库

优先使用 Arduino Core 自带：

```text
WiFi
WebServer
WebSockets
HTTPUpdate / Update
Preferences
HardwareSerial
Wire
```

MQTT可以选择成熟 Arduino MQTT 库。

JSON使用：

```text
ArduinoJson
```

但必须控制 JSON 内存占用。

不要无意义引入大量第三方库。

---

# 三十二、内存要求

必须监控：

```text
free heap
minimum free heap
largest free block
```

系统启动后打印：

```text
Heap:
Free:
Min:
Largest:
```

Web页面也显示。

严禁：

```text
大规模动态 String 拼接
无限增长 vector
无限增长日志
无限队列
```

UART日志必须有最大缓存限制。

---

# 三十三、错误处理

所有模块：

```cpp
begin()
```

必须返回：

```cpp
bool
```

失败时：

```text
记录 ERROR
报告模块状态
系统继续运行其它模块
```

例如：

```text
ADC failed
```

不能导致：

```text
WiFi
MQTT
UART
Web
```

全部停止。

---

# 三十四、启动流程

启动顺序：

```text
1. Arduino 初始化
2. Serial 初始化
3. GPIO Resource Manager
4. Preferences
5. Device ID
6. Log Manager
7. Event Bus
8. UART
9. ADC
10. GPIO Monitor
11. WiFi
12. Web Server
13. WebSocket
14. MQTT
15. OTA
16. Debug Gateway Ready
```

启动完成打印：

```text
====================================
ESP32-S3 Remote Hardware Debugger
====================================

Firmware: 1.0.0
Device ID: esp32s3-XXXXXXXX

WiFi: connected
IP: xxx.xxx.xxx.xxx
MQTT: connected

UART: ready
ADC: ready
GPIO: ready

Web: ready
WebSocket: ready
OTA: ready

System Ready
====================================
```

---

# 三十五、必须进行实际验证

不要只生成代码。

完成后必须：

1. 编译整个工程
2. 修复所有编译错误
3. 检查所有 GPIO
4. 检查内存
5. 检查任务
6. 检查 MQTT
7. 检查 WebSocket
8. 检查 UART
9. 检查 ADC
10. 检查配置保存
11. 检查 WiFi 重连
12. 检查 MQTT 重连
13. 检查 Web 多客户端
14. 检查 OTA
15. 检查异常情况

---

# 三十六、验收标准

必须满足：

## UART

```text
Target MCU TX
     ↓
ESP32-S3 RX
     ↓
Web
```

能够实时显示。

同时：

```text
Web
 ↓
ESP32-S3 TX
 ↓
Target MCU RX
```

能够发送。

---

## ADC

能够看到：

```text
CH0
CH1
CH2
CH3
```

实时电压。

---

## MQTT

能够通过 MQTT：

```text
查看 UART
查看 ADC
查看 GPIO
查看系统状态
```

并能够：

```text
UART TX
GPIO控制
```

---

## Web

浏览器能够：

```text
查看实时 UART
查看 ADC
查看 GPIO
发送 UART
控制 GPIO
配置 WiFi
配置 MQTT
执行 OTA
```

---

# 三十七、最终输出要求

开发完成后不要只告诉我“完成”。

必须输出：

```text
1. 完整工程目录
2. 所有源码文件
3. platformio.ini
4. GPIO配置表
5. 第三方依赖
6. 编译方法
7. 烧录方法
8. Web访问方法
9. MQTT Topic说明
10. MQTT JSON协议
11. WebSocket协议
12. UART接线说明
13. ADC接线说明
14. GPIO资源说明
15. FreeRTOS任务说明
16. 内存占用
17. 已完成测试
18. 已知限制
19. 后续扩展建议
```

如果发现硬件信息不足：

```text
不要猜测硬件连接。

优先采用可配置方案。

将不确定项集中放入：

config/pin_config.h
config/app_config.h
```

最终目标不是制作一个一次性的 ESP32 Arduino Demo，而是建立：

# ESP32-S3 Remote Hardware Debugger Framework

后续可以继续增加：

```text
UART
ADC
GPIO
I2C
SPI
CAN
RS485
USB
Logic Analyzer
Power Monitor
AI Agent
```

并保持：

```text
Web
MQTT
WebSocket
Event Bus
```

接口稳定。
