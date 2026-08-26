# ESP32-S3 N16R8 FreeRTOS 多任务设备监控器

## 一、项目目标

基于 **ESP32-S3 N16R8** 开发一个规范的 FreeRTOS 多任务设备监控器项目。

开发环境使用：

* Arduino IDE
* ESP32 Arduino Core
* FreeRTOS（使用 ESP32 Arduino Core 内置的 FreeRTOS）
* C/C++
* ESP32-S3 N16R8
* 串口通过 CH343 连接 PC
* UART0 使用 TXD0/RXD0

当前已经确认：

* ESP32-S3 可以正常运行
* CH343 → TXD0/RXD0 下载正常
* Arduino IDE 可以正常编译、烧录和运行

本项目的重点不是简单实现 LED 闪烁，而是建立一个**结构规范、任务职责清晰、具有 RTOS 思维的 ESP32-S3 基础工程**。

使用CH343的串口，CTS/RTS正确连接控制引脚，支持通过串口直接下载(连接COMx根据实际情况，沙箱允许访问查询)

---

## 二、功能要求

实现以下功能：

```text
ESP32-S3
│
├── Task_LED
├── Task_Button
├── Task_Monitor
├── Task_UART
└── Software Timer
```

要求合理使用：

* FreeRTOS Task
* Queue
* Semaphore
* Mutex
* Software Timer
* Heap
* PSRAM
* UART
* GPIO

---

## 三、任务设计

## 1. LED Task

创建：

```text
Task_LED
```

功能：

* 每 500 ms 翻转一次 LED
* 使用 FreeRTOS 延时
* 不允许使用 Arduino `delay()` 实现任务周期
* LED 控制封装成独立接口

建议接口：

```cpp
void led_init();
void led_set(bool on);
void led_toggle();
```

LED 引脚不要直接写死在任务代码中。

统一放到：

```text
config.h
```

中。

例如：

```cpp
#define LED_PIN ...
```

如果无法确定开发板板载 LED 的 GPIO，请先检查工程配置；如果无法确定，则提供明确的 `LED_PIN` 配置项，让用户自行修改。

---

## 四、Button Task

创建：

```text
Task_Button
```

功能：

* 周期性检测按键
* 检测按键按下事件
* 按键按下后，通过 FreeRTOS Queue 向 LED Task 或 Application Task 发送事件
* 不允许 Task 之间通过全局变量直接通信

定义事件：

```cpp
enum AppEvent {
    EVENT_BUTTON_PRESSED,
    EVENT_LED_TOGGLE,
};
```

使用：

```text
Button Task
    │
    │ Queue
    ▼
Application / LED Task
```

如果开发板没有明确可用的用户按键，则增加：

```cpp
#define BUTTON_PIN ...
```

配置项，并提供注释说明。

---

## 五、Queue

必须实际使用 FreeRTOS Queue。

创建：

```text
app_event_queue
```

用于任务之间传递事件。

例如：

```text
Task_Button
     │
     │ xQueueSend()
     ▼
app_event_queue
     │
     │ xQueueReceive()
     ▼
Task_LED / Application
```

要求：

* Queue 长度合理
* 发送失败时进行日志记录
* 不允许使用大量全局共享变量代替 Queue

---

## 六、Semaphore

必须实际使用 Semaphore。

设计一个合理的使用场景，例如：

```text
Button Task
      │
      ▼
Binary Semaphore
      │
      ▼
Application Task
```

或者使用 Mutex 保护某个共享资源。

需要明确区分：

### Binary Semaphore

用于：

```text
事件通知 / 同步
```

### Mutex

用于：

```text
共享资源互斥访问
```

不要为了“演示 API”而毫无意义地创建 Semaphore。

---

## 七、Mutex

创建一个用于保护共享资源的 Mutex。

推荐保护：

```text
UART Console
```

因为：

```text
Task_Monitor
Task_UART
其他 Task
      │
      ▼
 UART Console
```

可能同时打印日志。

设计：

```cpp
console_lock();
Serial.printf(...);
console_unlock();
```

确保日志不会因为多个任务同时输出而出现严重交错。

---

## 八、Task_UART

创建：

```text
Task_UART
```

功能：

从 UART0 接收简单命令。

例如：

```text
help
status
led on
led off
led toggle
heap
psram
tasks
```

示例：

```text
> help

Commands:
  help
  status
  led on
  led off
  led toggle
  heap
  psram
  tasks
```

例如：

```text
> led on
LED ON

> status
Uptime: 125 s
Free Heap: xxx KB
Free PSRAM: xxx KB
```

UART 使用：

```cpp
Serial
```

不要重新实现底层 UART 驱动。

---

## 九、Task_Monitor

创建：

```text
Task_Monitor
```

每 1 秒输出一次系统状态。

要求至少输出：

```text
[SYS] uptime: 125s
[SYS] free_heap: xxx KB
[SYS] min_free_heap: xxx KB
[SYS] free_psram: xxx KB
[SYS] min_free_psram: xxx KB
[SYS] cpu0: xx%
[SYS] cpu1: xx%
```

同时输出：

* FreeRTOS 当前任务数量
* 当前任务运行状态
* Task Stack High Water Mark
* CPU 核心信息
* Flash 信息
* PSRAM 是否可用

如果 ESP32 Arduino Core 没有直接提供准确的 CPU 使用率接口，不允许伪造数据。

可以改为输出：

```text
CPU0: available
CPU1: available
```

或者实现一个合理的 CPU Load 统计方案，并明确统计方法。

---

## 十、FreeRTOS Task 信息

增加一个系统任务统计功能。

通过 FreeRTOS API 获取：

```text
Task Name
State
Priority
Stack High Water Mark
Core ID
```

输出格式类似：

```text
[TASK]
Name        State   Priority   StackMin   Core
IDLE0       Ready   0          xxx        0
IDLE1       Ready   0          xxx        1
Task_LED    Block   2          xxx        1
Task_BTN    Block   2          xxx        1
Task_MON    Block   1          xxx        1
Task_UART   Block   2          xxx        1
```

注意：

不要假设所有 FreeRTOS API 在 Arduino ESP32 Core 的所有版本都可用。

编译前确认 API。

---

## 十一、Software Timer

必须实际使用 FreeRTOS Software Timer。

创建：

```text
SystemTimer
```

周期：

```text
1000 ms
```

Timer Callback 不执行复杂任务。

例如：

```text
SystemTimer
    │
    └── 每秒产生系统 Tick Event
              │
              ▼
        Monitor Task
```

或者实现一个系统 heartbeat 计数器。

注意：

**Timer Callback 中不要执行阻塞操作，不要进行大量 Serial 输出。**

---

## 十二、PSRAM

ESP32-S3 N16R8：

```text
Flash = 16 MB
PSRAM = 8 MB
```

程序必须实际检测 PSRAM。

启动时输出：

```text
[MEM] Flash: 16 MB
[MEM] PSRAM: 8 MB
```

同时输出：

```text
PSRAM Total
PSRAM Free
PSRAM Minimum Free
```

使用 ESP32 Arduino Core 提供的接口。

例如检查：

```cpp
psramFound()
ESP.getPsramSize()
ESP.getFreePsram()
```

具体 API 根据当前 ESP32 Arduino Core 版本确认。

---

## 十三、Heap 监控

启动时输出：

```text
[MEM] Free Heap
[MEM] Minimum Free Heap
```

每秒 Monitor Task 输出：

```text
[MEM] heap=xxxKB min=xxxKB
```

同时增加一个 Heap 测试命令：

```text
> heap
```

输出：

```text
Heap:
  Total: xxx KB
  Free: xxx KB
  Minimum Free: xxx KB
```

---

## 十四、CPU Core 使用

ESP32-S3 是双核 CPU。

需要体现双核 FreeRTOS 能力。

合理设置任务 Core Affinity。

建议：

```text
Core 0
├── ESP32 System / Network tasks
└── 部分系统任务

Core 1
├── Task_LED
├── Task_Button
├── Task_Monitor
└── Task_UART
```

但不要强行把 ESP-IDF/Arduino 系统任务全部绑定到 Core 0。

必须根据 ESP32 Arduino Core 当前运行机制合理设计。

任务创建时明确：

```text
Core 0
or
Core 1
```

并在任务信息中输出实际 Core ID。

---

## 十五、任务优先级

合理设计 Task Priority。

例如：

```text
Task_UART      Priority 3
Task_Button    Priority 3
Task_LED       Priority 2
Task_Monitor   Priority 1
```

但不要机械使用上述数值。

请根据实际功能解释为什么设置这样的优先级。

原则：

* 不需要高优先级的任务不要设置过高
* Monitor 不应该阻塞高优先级任务
* 不允许出现优先级倒置
* 使用 Mutex 时考虑优先级继承

---

## 十六、任务周期

建议：

```text
Task_LED
500 ms

Task_Button
20 ms

Task_Monitor
1000 ms

Task_UART
50 ms
```

必须使用：

```cpp
vTaskDelay()
```

或者：

```cpp
vTaskDelayUntil()
```

实现周期任务。

对于严格周期任务，优先考虑：

```cpp
vTaskDelayUntil()
```

不要使用：

```cpp
delay()
```

---

## 十七、工程结构

不要把所有代码写在：

```text
main.ino
```

里面。

建立清晰的模块结构。

建议：

```text
ESP32S3_FreeRTOS_Monitor/
│
├── ESP32S3_FreeRTOS_Monitor.ino
│
├── config/
│   └── config.h
│
├── app/
│   ├── app.cpp
│   └── app.h
│
├── tasks/
│   ├── task_led.cpp
│   ├── task_led.h
│   ├── task_button.cpp
│   ├── task_button.h
│   ├── task_monitor.cpp
│   ├── task_monitor.h
│   ├── task_uart.cpp
│   └── task_uart.h
│
├── drivers/
│   ├── led.cpp
│   ├── led.h
│   ├── button.cpp
│   └── button.h
│
├── system/
│   ├── system_info.cpp
│   └── system_info.h
│
└── README.md
```

要求：

* 驱动层不依赖 Application
* Task 层调用 Driver
* Application 负责系统组织
* Config 统一管理 GPIO 和参数
* 不允许出现大量跨模块 extern 全局变量

---

## 十八、日志系统

统一日志格式。

例如：

```text
[INFO] System started
[INFO] ESP32-S3 N16R8 detected
[INFO] PSRAM: 8 MB
[INFO] Task_LED started
[INFO] Task_Button started
[INFO] Task_Monitor started
[INFO] Task_UART started
```

系统状态：

```text
[SYS] uptime=125s heap=312KB psram=7200KB
```

按键：

```text
[BTN] pressed
```

错误：

```text
[ERR] Queue send failed
```

不要在各个模块中随意使用不同的打印格式。

---

## 十九、启动输出

上电后希望看到类似：

```text
========================================
 ESP32-S3 FreeRTOS Monitor
========================================

Chip:
  Model: ESP32-S3
  CPU Cores: 2
  CPU Frequency: 240 MHz

Memory:
  Flash: 16 MB
  PSRAM: 8 MB
  Free Heap: xxx KB
  Free PSRAM: xxx KB

FreeRTOS:
  Scheduler: running

Tasks:
  LED     : started
  Button  : started
  Monitor : started
  UART    : started

System ready.
========================================
```

---

## 二十、异常处理

必须考虑：

* Queue 创建失败
* Task 创建失败
* Mutex 创建失败
* Semaphore 创建失败
* Timer 创建失败
* PSRAM 不存在
* Heap 不足

例如：

```cpp
if (taskHandle == nullptr) {
    Serial.println("[ERR] Failed to create Task_LED");
}
```

出现严重初始化失败时，应明确输出错误信息。

---

## 二十一、README

创建完整 README.md。

内容至少包括：

1. 项目简介

2. 硬件

```text
ESP32-S3 N16R8
CH343
UART0
LED
Button
```

3. 软件环境

```text
Arduino IDE
ESP32 Arduino Core
FreeRTOS
```

4. 编译方法

5. 烧录方法

6. 串口参数

```text
115200 8N1
```

7. 功能说明

8. FreeRTOS 架构

9. Task 列表

10. UART 命令

11. 内存监控

12. 常见问题

---

## 二十二、开发要求

开发过程中遵循以下原则：

### 1. 不要过度复杂化

这是第一个 ESP32-S3 FreeRTOS 项目。

不要加入：

* Wi-Fi
* MQTT
* LVGL
* OTA
* BLE
* Camera
* AI

这些功能留到后续项目。

本项目专注：

```text
FreeRTOS
+
GPIO
+
UART
+
Memory
+
PSRAM
+
Task
+
Queue
+
Semaphore
+
Mutex
+
Timer
```

---

### 2. 不要伪造硬件信息

如果无法确定：

```text
LED GPIO
Button GPIO
```

不要猜测。

将它们做成：

```cpp
#define LED_PIN ...
#define BUTTON_PIN ...
```

并在 README 中说明需要根据开发板实际原理图修改。

---

### 3. 优先保证可编译

代码完成后必须实际进行：

```text
编译
 ↓
检查错误
 ↓
修复错误
 ↓
再次编译
```

不要只提供理论代码。

---

## 二十三、验收标准

项目完成后必须满足：

### 基础功能

* [ ] Arduino IDE 可以正常编译
* [ ] 可以通过 CH343 + UART0 下载
* [ ] ESP32-S3 正常启动
* [ ] LED 每 500 ms 翻转
* [ ] 按键可以产生事件
* [ ] Queue 正常工作
* [ ] Semaphore 正常工作
* [ ] Mutex 正常工作
* [ ] Software Timer 正常工作
* [ ] UART 命令正常工作

### 系统监控

* [ ] CPU 双核信息正常
* [ ] Task 信息正常
* [ ] Stack High Water Mark 正常
* [ ] Free Heap 正常
* [ ] Minimum Free Heap 正常
* [ ] PSRAM 检测正常
* [ ] Free PSRAM 正常
* [ ] Uptime 正常

### 工程质量

* [ ] 模块化代码
* [ ] Task 与 Driver 分离
* [ ] 配置统一
* [ ] 日志统一
* [ ] README 完整
* [ ] 无明显全局变量滥用
* [ ] 无 `delay()` 作为 FreeRTOS Task 周期控制
* [ ] 无伪造 CPU 使用率
* [ ] 无未使用的大型模块

---

## 二十四、最终输出

开发完成后，请向我汇报：

1. 工程目录结构
2. 使用的 Arduino ESP32 Core 版本
3. 使用的 GPIO
4. Task 架构
5. Task 优先级
6. Task Core Affinity
7. Queue 设计
8. Semaphore 设计
9. Mutex 设计
10. Software Timer 设计
11. Heap 使用情况
12. PSRAM 使用情况
13. UART 命令列表
14. 实际编译结果
15. 实际烧录结果
16. 实际运行日志
17. 遇到的问题及解决方法

特别要求：

**不要只告诉我“代码应该可以运行”，必须实际编译验证。**

如果当前环境无法直接连接 ESP32-S3 硬件，则至少完成：

```text
代码生成
→ Arduino IDE 工程检查
→ 编译
→ 错误修复
→ 最终编译验证
```

并明确说明：

```text
[BUILD] PASS
[FLASH] 未连接硬件，无法验证
[RUN] 未连接硬件，无法验证
```

不要把未实际验证的结果描述为已经验证。
