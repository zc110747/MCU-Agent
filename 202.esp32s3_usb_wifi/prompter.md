# ESP32-S3 USB RNDIS Wi-Fi 网卡 / USB 网络共享项目

## 一、项目目标

使用 ESP32-S3 开发一个 USB Wi-Fi 网络适配器。

使用CH343的串口，CTS/RTS正确连接控制引脚，支持通过串口直接下载(连接COMx根据实际情况，沙箱允许访问查询)，尽可能参考github已有项目和资源进行复刻

ESP32-S3 自带 Wi-Fi，通过 USB Device 连接 Windows PC，在 Windows 中枚举为一个 RNDIS USB 网络适配器。

整体功能：

```text
                    Internet
                       │
                       │ Wi-Fi
                       ↓
                 ┌─────────────┐
                 │  ESP32-S3   │
                 │             │
                 │ Wi-Fi STA   │
                 │    │        │
                 │    ↓        │
                 │    lwIP      │
                 │    │        │
                 │    ↓        │
                 │   RNDIS      │
                 └─────┬───────┘
                       │ USB
                 GPIO19 / GPIO20
                       │
                       ↓
                  Windows 10/11
                       │
                 RNDIS 网卡
                       │
                       ↓
                    网络访问
```

项目不考虑 USB 高速传输性能，优先保证：

1. 功能稳定
2. Windows 10/11 兼容
3. USB RNDIS 正确枚举
4. Wi-Fi 配置简单
5. 掉线自动恢复
6. USB 拔插稳定
7. 工程结构清晰
8. 方便后续扩展 Web 配置功能

---

# 二、硬件要求

MCU：

* ESP32-S3
* 使用 ESP32-S3 原生 USB OTG/USB Device
* USB D-：GPIO19
* USB D+：GPIO20

USB：

```text
USB-C
 ├── D- → GPIO19
 ├── D+ → GPIO20
 ├── GND
 └── VBUS
```

根据 ESP32-S3 USB Device 硬件要求设计 USB-C 电路。

不要把 GPIO19/GPIO20 当普通 GPIO 使用。

检查 ESP-IDF 对 ESP32-S3 USB Device 的官方定义，确认 GPIO19/GPIO20 的 USB 功能配置正确。

---

# 三、软件环境

优先使用：

* ESP-IDF
* C/C++
* FreeRTOS
* lwIP
* TinyUSB
* ESP32-S3 USB Device
* Wi-Fi STA

不要为了实现 RNDIS 自己重新实现完整 USB Device Stack。

优先使用 ESP-IDF 已集成的 TinyUSB/USB Device 能力。

开发之前先检查当前 ESP-IDF 版本实际支持情况。

特别确认：

1. ESP32-S3 USB Device 是否支持 RNDIS
2. TinyUSB 当前版本是否提供 RNDIS Network Device Class
3. ESP-IDF 是否已经提供 RNDIS 示例
4. 如果没有完整 RNDIS，需要设计最小可用 RNDIS Device Class
5. Windows 10/11 对当前 USB Descriptor 和 RNDIS 实现的兼容性

不要凭经验假设 API 存在。

必须以当前工程实际安装的 ESP-IDF/TinyUSB API 为准。

---

# 四、核心功能

## 4.1 Wi-Fi STA

ESP32-S3 启动后进入 Wi-Fi STA 模式。

默认状态：

```text
ESP32-S3
   ↓
Wi-Fi STA
   ↓
连接用户指定的 AP
   ↓
获取 DHCP 地址
   ↓
Internet
```

需要支持：

* SSID
* Password
* WPA/WPA2
* DHCP
* Wi-Fi 自动重连
* Wi-Fi 断线检测
* Wi-Fi 重连
* 获取 IP
* 获取 Gateway
* 获取 DNS

---

# 五、USB RNDIS

USB Device 使用 RNDIS。

Windows 连接后应该出现：

```text
网络适配器
└── USB RNDIS Network Adapter
```

或者 Windows 对应的 RNDIS 网络适配器名称。

要求：

1. USB 正常枚举
2. Windows 能识别 RNDIS
3. 不要求用户安装额外驱动
4. 能获取 IPv4 地址
5. 能访问 ESP32-S3
6. 能访问 Internet
7. Windows 拔插 USB 后可以自动重新枚举
8. USB 重新连接后网络自动恢复

---

# 六、网络架构

建议实现：

```text
Windows
   │
   │ Ethernet Frame
   ↓
RNDIS
   │
   ↓
USB
   │
   ↓
ESP32-S3 TinyUSB
   │
   ↓
Network Interface
   │
   ↓
lwIP
   │
   ↓
Wi-Fi STA
   │
   ↓
Wi-Fi AP
   │
   ↓
Internet
```

重点分析两种网络模式：

### 模式 A：NAT

推荐优先实现。

```text
Windows
192.168.4.x
    │
    ↓
ESP32-S3
192.168.4.1
    │
    ↓
Wi-Fi STA
192.168.1.x
    │
    ↓
Router
    │
    ↓
Internet
```

ESP32-S3 作为 NAT Router。

需要实现：

```text
USB RNDIS Interface
        │
        ↓
ESP32 Network Stack
        │
        ├── DHCP Server
        │
        ├── DNS Proxy/Forward
        │
        ├── NAT
        │
        ↓
Wi-Fi STA
```

如果 ESP-IDF/lwIP 当前环境没有现成 NAT 功能，需要调查可用组件。

不要为了追求复杂功能而改变整个网络架构。

---

# 七、Windows 侧配置

目标是让 Windows 用户可以通过常规网络配置界面配置网络。

理想使用方式：

```text
ESP32-S3 插入 Windows
        ↓
Windows 枚举 RNDIS 网卡
        ↓
自动获取 IP
        ↓
用户配置 Wi-Fi
        ↓
ESP32-S3 连接 Wi-Fi
        ↓
Windows 获得 Internet
```

Wi-Fi 配置建议提供两种方式。

## 方案 1：USB RNDIS + Web 配置

推荐。

ESP32-S3 启动后建立：

```text
USB RNDIS
    ↓
192.168.4.1
    ↓
HTTP Server
    ↓
Windows 浏览器
    ↓
Wi-Fi 配置页面
```

用户访问：

```text
http://192.168.4.1
```

页面提供：

```text
Wi-Fi Configuration

SSID:
[________________]

Password:
[________________]

[Scan Wi-Fi]

[Connect]

Status:
Connected

IP:
192.168.x.x
```

这样不需要额外 Windows 软件。

---

## 方案 2：Windows 网络共享中心

需要明确：

“Windows 网络共享中心”本身主要负责 Windows 网络适配器、IP、共享和连接管理，并不是通用的 Wi-Fi SSID/密码下发协议。

因此不要错误地假设：

```text
Windows 网络共享中心
        ↓
自动把 Wi-Fi SSID/密码发送给 ESP32
```

除非能够找到明确的 Windows 标准机制支持。

如果不存在标准机制，采用：

```text
Windows
   ↓
RNDIS
   ↓
HTTP
   ↓
ESP32-S3
   ↓
Wi-Fi 配置
```

作为正式方案。

---

# 八、推荐的配置流程

设备第一次启动：

```text
ESP32-S3
   ↓
Wi-Fi 没有保存配置
   ↓
启动 USB RNDIS
   ↓
USB 网卡获得：
192.168.4.1
   ↓
启动 DHCP Server
   ↓
Windows 自动获得：
192.168.4.x
   ↓
用户浏览器访问：
http://192.168.4.1
   ↓
扫描 Wi-Fi
   ↓
选择 SSID
   ↓
输入密码
   ↓
ESP32-S3 保存配置
   ↓
连接 Wi-Fi
   ↓
启动 NAT
   ↓
Windows 获得 Internet
```

后续启动：

```text
ESP32-S3
   ↓
读取 NVS
   ↓
Wi-Fi 自动连接
   ↓
USB RNDIS
   ↓
Windows DHCP
   ↓
直接上网
```

---

# 九、Wi-Fi 配置页面

实现一个简单 Web UI。

页面至少包含：

### 状态

```text
Wi-Fi Status
Connected / Disconnected

SSID
RSSI
Channel
IP Address
Gateway
DNS
```

### Wi-Fi 扫描

显示：

```text
SSID
RSSI
Channel
Security
```

用户可以点击 SSID。

### Wi-Fi 配置

```text
SSID
Password

[Connect]
```

### 网络状态

显示：

```text
USB:
Connected

RNDIS:
Up

Wi-Fi:
Connected

ESP32 IP:
192.168.1.100

USB IP:
192.168.4.1

Gateway:
192.168.1.1

DNS:
192.168.1.1
```

---

# 十、NVS 配置

Wi-Fi 配置保存到 NVS。

例如：

```text
wifi.ssid
wifi.password
```

不要明文打印密码到日志。

日志中密码必须隐藏：

```text
SSID: MyWiFi
Password: ******
```

支持恢复出厂设置：

```text
Factory Reset
```

清除：

* Wi-Fi SSID
* Wi-Fi Password
* 网络配置
* Web 配置

---

# 十一、RNDIS 实现要求

重点检查 TinyUSB 当前版本。

RNDIS 至少需要支持：

```text
REMOTE_NDIS_INITIALIZE_MSG
REMOTE_NDIS_HALT_MSG
REMOTE_NDIS_QUERY_MSG
REMOTE_NDIS_SET_MSG
REMOTE_NDIS_RESET_MSG
REMOTE_NDIS_KEEPALIVE_MSG
```

以及：

```text
REMOTE_NDIS_PACKET_MSG
```

需要正确实现：

```text
USB Control Endpoint
USB Bulk IN
USB Bulk OUT
```

并正确处理：

```text
RNDIS Initialize
RNDIS Query
RNDIS Set
RNDIS KeepAlive
RNDIS Packet
```

必须参考实际 TinyUSB RNDIS 实现，而不是自行猜测协议格式。

---

# 十二、USB Descriptor

正确配置：

```text
Device Descriptor
Configuration Descriptor
Interface Descriptor
RNDIS Control Interface
RNDIS Data Interface
CDC/RNDIS Functional Descriptor
Endpoint Descriptor
```

根据 Windows RNDIS 设备要求配置：

```text
VID
PID
Manufacturer
Product
Serial Number
```

开发阶段可以使用项目专用 VID/PID，但正式产品必须使用合法 VID/PID。

不要随意使用其他厂商 VID/PID。

---

# 十三、Windows 兼容性测试

至少测试：

### Windows 11

```text
USB 插入
↓
设备管理器
↓
网络适配器
↓
RNDIS
```

检查：

* 是否自动安装驱动
* 是否出现黄色感叹号
* 是否能够获得 IP
* 是否能够 ping ESP32
* 是否能够访问 Web
* 是否能够 ping 网关
* 是否能够访问 Internet

### Windows 10

执行同样测试。

---

# 十四、网络测试

测试：

```text
ping 192.168.4.1
```

然后：

```text
ping 8.8.8.8
```

然后：

```text
nslookup www.baidu.com
```

然后浏览器访问：

```text
https://www.baidu.com
```

检查：

```text
ARP
DHCP
DNS
ICMP
TCP
UDP
HTTP
HTTPS
```

---

# 十五、Wi-Fi 断线恢复

必须实现：

```text
Wi-Fi Connected
      │
      ↓
Wi-Fi Disconnect
      │
      ↓
自动重连
      │
      ↓
重新获得 IP
      │
      ↓
恢复 NAT
      │
      ↓
Windows 网络恢复
```

USB 不需要重新插拔。

如果 Wi-Fi 临时断开：

* RNDIS 不应崩溃
* USB 不应重新枚举
* Web Server 不应崩溃
* DHCP Server 应继续工作
* Wi-Fi 恢复后网络自动恢复

---

# 十六、USB 拔插恢复

测试：

```text
ESP32-S3
    ↓
USB connected
    ↓
Windows RNDIS
    ↓
拔出 USB
    ↓
重新插入
    ↓
重新枚举
    ↓
RNDIS 恢复
```

不能出现：

* USB Device 卡死
* TinyUSB Task 死锁
* lwIP 网络接口异常
* FreeRTOS Task 泄漏
* 重插后无法枚举

---

# 十七、FreeRTOS 任务规划

建议划分：

```text
main
 │
 ├── USB/TinyUSB Task
 │
 ├── Wi-Fi Task
 │
 ├── Network Task
 │
 ├── Web Server
 │
 └── Status Monitor
```

不要创建大量没有必要的 Task。

尽量使用：

```text
Event Group
Queue
Semaphore
Event Loop
```

进行模块通信。

---

# 十八、网络接口设计

建议抽象：

```c
usb_netif
wifi_netif
```

逻辑：

```text
                ┌─────────────┐
                │   lwIP      │
                └──────┬──────┘
                       │
             ┌─────────┴─────────┐
             │                   │
        usb_netif             wifi_netif
             │                   │
          RNDIS                  Wi-Fi
             │                   │
           USB                  ESP32
```

如果使用 NAT：

```text
usb_netif
    ↓
NAT
    ↓
wifi_netif
```

---

# 十九、代码工程结构

推荐：

```text
esp32s3-rndis-wifi/
│
├── CMakeLists.txt
├── sdkconfig
├── sdkconfig.defaults
│
├── main/
│   ├── main.c
│   │
│   ├── usb/
│   │   ├── usb_rndis.c
│   │   ├── usb_rndis.h
│   │   ├── usb_descriptors.c
│   │   └── usb_descriptors.h
│   │
│   ├── wifi/
│   │   ├── wifi_manager.c
│   │   └── wifi_manager.h
│   │
│   ├── network/
│   │   ├── network_manager.c
│   │   ├── network_manager.h
│   │   └── nat.c
│   │
│   ├── web/
│   │   ├── web_server.c
│   │   ├── web_server.h
│   │   └── web_ui.c
│   │
│   ├── config/
│   │   ├── config_manager.c
│   │   └── config_manager.h
│   │
│   └── system/
│       ├── system_manager.c
│       └── system_manager.h
│
└── components/
```

如果 ESP-IDF/TinyUSB 已经提供成熟 RNDIS 组件，不要重复实现。

优先封装现有组件。

---

# 二十、开发顺序

不要一次性实现所有功能。

严格按照下面阶段开发。

## Phase 1：USB Device

首先只实现：

```text
ESP32-S3
   ↓
USB Device
   ↓
Windows
```

确认 USB 能稳定枚举。

---

## Phase 2：RNDIS

实现：

```text
ESP32-S3
   ↓
RNDIS
   ↓
Windows
```

Windows 出现 RNDIS 网络适配器。

先不连接 Wi-Fi。

使用静态 IP 测试：

```text
Windows
192.168.4.2

ESP32
192.168.4.1
```

实现：

```text
ping 192.168.4.1
```

---

## Phase 3：DHCP

实现：

```text
Windows
    ↓
DHCP
    ↓
ESP32
```

Windows 自动获取：

```text
IP
Mask
Gateway
DNS
```

---

## Phase 4：Web Server

Windows 访问：

```text
http://192.168.4.1
```

出现配置页面。

---

## Phase 5：Wi-Fi STA

实现：

```text
ESP32
 ↓
Wi-Fi STA
 ↓
Internet
```

---

## Phase 6：NAT

实现：

```text
Windows
   ↓
RNDIS
   ↓
ESP32
   ↓
NAT
   ↓
Wi-Fi
   ↓
Internet
```

最终实现：

```text
Windows
   ↓
USB
   ↓
ESP32-S3
   ↓
Wi-Fi
   ↓
Internet
```

---

# 二十一、日志要求

使用 ESP-IDF logging：

```c
ESP_LOGI()
ESP_LOGW()
ESP_LOGE()
ESP_LOGD()
```

关键状态必须输出。

例如：

```text
USB connected
RNDIS initialized
RNDIS link up
DHCP server started
WiFi connecting...
WiFi connected
IP: 192.168.1.100
Gateway: 192.168.1.1
DNS: 192.168.1.1
NAT started
Network ready
```

不要打印：

* Wi-Fi 密码
* 敏感配置
* 完整 RNDIS 数据包内容

---

# 二十二、异常处理

必须考虑：

```text
Wi-Fi 无 AP
Wi-Fi 密码错误
Wi-Fi AP 消失
Wi-Fi DHCP 失败
USB 插入
USB 拔出
USB 重连
Windows 睡眠
Windows 唤醒
RNDIS 初始化失败
RNDIS KeepAlive 超时
DHCP Client 重启
DHCP Server 重启
NAT 异常
Web Server 异常
```

系统不能因为单个模块异常导致整个 ESP32-S3 重启。

---

# 二十三、性能要求

本项目明确：

**不以 USB 速度为主要指标。**

优先级：

```text
稳定性
  >
Windows 兼容性
  >
功能完整性
  >
代码可维护性
  >
性能
```

不需要为了追求速度实现复杂的 DMA/零拷贝优化。

但必须避免：

* 内存泄漏
* Buffer Overflow
* Deadlock
* Task starvation
* Watchdog reset

---

# 二十四、最终验收标准

完成后必须满足：

### USB

* [ ] GPIO19 = USB D-
* [ ] GPIO20 = USB D+
* [ ] Windows 10 可识别
* [ ] Windows 11 可识别
* [ ] USB 拔插恢复正常

### RNDIS

* [ ] RNDIS 初始化成功
* [ ] Windows 出现网络适配器
* [ ] DHCP 正常
* [ ] Windows 可以 ping ESP32
* [ ] Windows 可以访问 ESP32 Web 页面

### Wi-Fi

* [ ] 可以扫描 AP
* [ ] 可以配置 SSID
* [ ] 可以配置密码
* [ ] 可以保存配置
* [ ] 可以自动重连
* [ ] 可以获取 Wi-Fi IP

### 网络共享

* [ ] Windows → USB RNDIS
* [ ] ESP32-S3 → Wi-Fi
* [ ] NAT 正常
* [ ] DNS 正常
* [ ] Internet 正常
* [ ] Wi-Fi 断线可以自动恢复

### 配置

* [ ] Web UI
* [ ] Wi-Fi 扫描
* [ ] SSID 配置
* [ ] 密码配置
* [ ] 连接状态
* [ ] IP 状态
* [ ] 恢复出厂设置

---

# 二十五、开发 Agent 的重要要求

在开始修改代码之前：

1. 检查当前工程目录。
2. 检查 ESP-IDF 版本。
3. 检查 ESP-IDF 当前 TinyUSB 版本。
4. 搜索当前 SDK 是否已经提供 RNDIS。
5. 搜索官方 ESP-IDF/TinyUSB 示例。
6. 不允许凭空编造 API。
7. 不允许因为某个 API 不存在就随意修改架构。
8. 优先复用官方组件。
9. 每完成一个 Phase 都必须编译。
10. 每个阶段都进行实际硬件验证。
11. 编译出现错误时先分析实际 API 和 SDK 版本，不要盲目修改。
12. 所有修改必须保持工程可以继续构建。

最终输出：

```text
1. 项目架构
2. 使用的 ESP-IDF 版本
3. TinyUSB 版本
4. RNDIS 实现方式
5. USB Descriptor 设计
6. 网络架构
7. NAT 实现方式
8. Wi-Fi 配置方式
9. Web UI
10. Windows 测试结果
11. USB 拔插测试结果
12. Wi-Fi 断线恢复测试结果
13. 已知问题
14. 后续优化建议
```

**不要只生成代码。必须实际编译、烧录并进行能够执行的验证。**
