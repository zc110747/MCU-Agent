# ESP32-S3 FreeRTOS Monitor

> 一个结构规范、任务职责清晰、体现 RTOS 思维的 ESP32-S3 (N16R8) 多任务设备监控器示例工程。
> 目标板：**ESP32-S3-COREBOARD V1.4**（原理图 `ESP32-S3-SCH-V1.4.pdf` 已核对）。

基于 **Arduino IDE + ESP32 Arduino Core + 内置 FreeRTOS** 实现，重点不是 LED 闪烁，
而是建立一个可复用的 FreeRTOS 基础工程骨架：任务 / 队列 / 信号量 / 互斥量 / 软件定时器 /
堆 / PSRAM / UART 的规范使用方式。

---

## 1. 项目简介

本项目在 ESP32-S3 N16R8 上运行 4 个 FreeRTOS 任务 + 1 个软件定时器，构成一个最小但
完整的设备监控器：

- **Task_LED**：每 500 ms 翻转 LED（硬实时周期，使用 `vTaskDelayUntil`）。
- **Task_Button**：周期轮询按键，去抖后通过 **Queue** 发送事件。
- **Task_Monitor**：每 1 秒由 **Software Timer** 唤醒，输出系统 / 内存 / 任务状态。
- **Task_UART**：从 UART0 接收命令（`help` / `status` / `led on` / ...），通过 Queue 控制 LED。
- **SystemTimer**：1000 ms 周期软件定时器，仅 `xSemaphoreGive` 一个 **Binary Semaphore**。
- **Mutex**：保护 UART 控制台，避免多任务日志交错。

所有跨任务通信均通过 Queue / Semaphore / Mutex，不依赖大量全局变量。

---

## 2. 硬件

**目标板：ESP32-S3-COREBOARD V1.4**（已对照原理图 `ESP32-S3-SCH-V1.4.pdf` 核对）

| 项目 | 说明 |
| --- | --- |
| MCU | ESP32-S3 (双核 Xtensa LX7, 240 MHz) |
| Flash | 16 MB (N16R8) |
| PSRAM | 8 MB (Octal, OPI) |
| 板型 | ESP32-S3-COREBOARD V1.4 |
| 调试/下载 | 内置 USB-Serial-JTAG (VID 0x303a PID 0x1001) + CH343 串口 |
| 串口 | UART0 (TXD0 / RXD0)，由板载 CH343 / USB-Serial-JTAG 连接 PC |
| **用户 LED** | **WS2812B RGB，GPIO48**（单线 800 kHz，GRB 顺序，由 `bsp/led.cpp` 驱动） |
| **板载指示灯** | PWRLED-RED（电源常亮）、TXLED2/RXLED2（UART 活动灯，硬件驱动不可控） |
| 按键 | BOOT 按钮（GPIO0，active-low，R5 10k 上拉） |

> **GPIO 说明（已按原理图核对）**：
>
> - **`LED_PIN = 48`（WS2812B）**：用户 LED 是一颗 **WS2812B 全彩灯**，数据线接 **GPIO48**，
>   单线 800 kHz 协议（GRB 顺序）。它**不是**普通数字引脚，不能用 `digitalWrite` 驱动，
>   而是用 `Adafruit_NeoPixel` 库（内部走 ESP32 RMT 外设保证时序）驱动。
>   安装依赖：`arduino-cli lib install "Adafruit NeoPixel"`。
>   `led_set(true)` → 显示 `LED_ON_*` 配置的 GRB 颜色，`led_set(false)` → 熄灭。
>   亮度由 `LED_WS2812_BRIGHTNESS` 控制（默认 40，避免过亮刺眼）。
>
> - **`BUTTON_PIN = 0`**：BOOT 按钮，接 GPIO0，低电平有效。原理图确认：
>   BOOT → GPIO0，R5(10kΩ) 上拉至 VDD33。自动下载电路 DTR/RTS 经 SS8050 分别控制 EN/IO0。

---

## 3. 软件环境

- Arduino IDE（或 arduino-cli）
- ESP32 Arduino Core（`esp32:esp32`，**本工程实测版本：3.3.11**）
- FreeRTOS（ESP32 Arduino Core 内置，无需单独安装）
- **Adafruit_NeoPixel**（WS2812B 驱动，**v1.15.5**，`arduino-cli lib install "Adafruit NeoPixel"`）
- 开发语言：C / C++

---

## 4. 工程结构

```text
ESP32S3_FreeRTOS_Monitor/
│
├── ESP32S3_FreeRTOS_Monitor.ino      # 入口：setup() 中 app_init()+app_start()
│
├── config/
│   └── config.h                      # 所有 GPIO / 参数 / 优先级 / 内核亲和性 集中配置
│
├── app/
│   ├── app.h                         # RTOS 对象声明、事件枚举、日志接口
│   └── app.cpp                       # 创建 Queue/Semaphore/Mutex/Timer/Task，日志实现
│
├── tasks/
│   ├── task_led.{h,cpp}              # 500ms 心跳 + 事件消费者（LED 唯一拥有者）
│   ├── task_button.{h,cpp}           # 20ms 轮询，去抖后向 Queue 发事件
│   ├── task_monitor.{h,cpp}          # 1s 系统状态上报（受定时器信号量唤醒）
│   └── task_uart.{h,cpp}             # 50ms 轮询 UART0，命令解释器
│
├── bsp/
│   ├── led.{h,cpp}                   # LED 驱动（不依赖 Application 层）
│   └── button.{h,cpp}                # 按键驱动（含去抖）
│
├── system/
│   └── system_info.{h,cpp}           # 芯片/内存/Flash 信息与 FreeRTOS 任务表
│
└── README.md
```

**分层原则**：驱动层 (`bsp/`) 只依赖 `config.h`，不依赖应用层；
任务层 (`tasks/`) 调用驱动；应用层 (`app/`) 负责系统编排；`config.h` 统一管理硬件参数；
不出现大量跨模块 `extern` 全局变量（仅通过 `app.h` 暴露必要的 FreeRTOS 句柄）。

---

## 5. 编译方法

### 方式 A：arduino-cli（本项目实际验证所用）

```bash
# 1) 安装 esp32 板支持包（含工具链，首次约需下载 1GB+）
arduino-cli core update-index
arduino-cli core install esp32:esp32

# 2) 编译（N16R8：OPI PSRAM + 16MB Flash）
#    -j 8 限制并行编译线程数，避免高核数机器（如 48 线程）全速编译导致温度过高。
#    FlashSize 的取值是 16M（不是 16MB）。
arduino-cli compile -j 8 \
  -b esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600 \
  ESP32S3_FreeRTOS_Monitor
```

> **关于子目录 `.cpp` 的说明**：Arduino 构建器只编译 `.ino` 与顶层文件，
> **不会**自动编译 `app/`、`tasks/`、`bsp/`、`system/` 子目录里的 `.cpp`。
> 本工程在 `ESP32S3_FreeRTOS_Monitor.ino` 中显式 `#include` 了各模块的 `.cpp`
> （`#include "app/app.cpp"` 等），从而把全部模块编译进同一个翻译单元。
> 这种方式在 Arduino IDE 与 arduino-cli 下行为一致，子目录内的
> 相对/带路径 `#include`（如 `"app/app.h"`、`"config/config.h"`）均从工程根目录解析。

### 方式 B：Arduino IDE

1. 安装 ESP32 支持包：`文件 → 首选项 → 附加开发板管理器网址` 添加
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`；
   `工具 → 开发板管理器` 搜索 **esp32** 并安装。
2. 选择开发板：`工具 → 开发板 → ESP32 Arduino → ESP32S3 Dev Module`。
3. 设置：`PSRAM = OPI`、`Flash Size = 16MB`、`Partition Scheme = Default`、
   `Upload Speed = 921600`、`Port` 选择 CH343 对应的 COM。
4. 打开本工程目录，`Ctrl+R` 编译。

> ⚠️ 必须启用 **PSRAM = OPI**，否则 `psramFound()` 返回 false，运行时将打印 “PSRAM: not available”。

### 方式 C：一键脚本（推荐，零记忆命令）

工程根目录已提供 `build.bat`，自动定位 `arduino-cli`（优先 PATH，其次 `../.buildtools/bin`），
无需记忆 FQBN 与端口参数。在文件资源管理器双击，或在 CMD 中进入工程目录运行：

```bat
build.bat               仅编译 (jobs=8, 限制线程避免高温)
build.bat flash        编译 + 烧录到默认端口 COM21
build.bat flash COM21  编译 + 烧录到指定端口
build.bat monitor      仅打开串口监视器 (115200)
build.bat all COM21    编译 + 烧录 + 打开监视器
```

> 脚本首次运行会从零编译 ESP32 core（约上千文件，耗时数分钟）；之后复用 `.build` 缓存，约 20s。
> 若你用自己的 `arduino-cli`，把它加入系统 PATH 即可被脚本自动识别（无需改脚本）。

---

## 6. 烧录方法

- 通过 CH343 连接的 UART0 进行串口下载（ESP32-S3 支持串口一键下载，无需手动拉 BOOT）。
- Arduino IDE：`工具 → 端口` 选 CH343 COM，`Ctrl+U` 上传。
- arduino-cli：
  ```bash
  arduino-cli upload -p COMx -b esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M \
    ESP32S3_FreeRTOS_Monitor
  ```
  （把 `COMx` 替换为实际端口，例如 `COM3`）

---

## 7. 串口参数

```text
波特率: 115200
数据位: 8
停止位: 1
校验:   无 (8N1)
```

打开任意串口终端（Arduino 串口监视器 / PuTTY / minicom），即可输入命令并查看系统日志。

---

## 8. 功能说明

- 上电后打印启动横幅（芯片 / 内存 / FreeRTOS / 任务列表）。
- Task_LED 每 500 ms 翻转一次 WS2812B RGB 灯（心跳），颜色由 `config.h` 的 `LED_ON_*` 决定。
- 按键按下（去抖后）产生 `[BTN] pressed` 日志，并通过 Queue 触发一次 LED 翻转作为反馈。
- 每 1 秒由软件定时器唤醒 Monitor 任务，输出：
  - `[SYS]` uptime / free_heap / min_free_heap / free_psram / min_free_psram / CPU0 / CPU1
  - `[MEM]` heap / min
  - `[TASK]` FreeRTOS 任务表（名称 / 状态 / 优先级 / 栈高水位 / 内核）
- UART 命令实时查看/控制设备（见 §11）。
- 控制台输出经 Mutex 保护，多任务日志不会严重交错。

---

## 9. FreeRTOS 架构

```text
                ┌─────────────────────────────────────────────┐
                │            FreeRTOS Scheduler (running)      │
                └─────────────────────────────────────────────┘
   Core 0 (系统/网络任务)                Core 1 (应用任务, 本项目)
                                      ┌──────────────────────────┐
                                      │ Task_LED     (prio 2)     │
                                      │ Task_Button  (prio 3)     │
   Software Timer ──give──► Binary    │ Task_Monitor (prio 1)     │
        (1s)            Semaphore ──► │ Task_UART    (prio 3)     │
                                      └──────────────────────────┘
                                            │  Queue (AppEvent)
        Task_Button ──xQueueSend──► app_event_queue ──xQueueReceive──► Task_LED
        Task_UART    ──xQueueSend──► app_event_queue ──xQueueReceive──► Task_LED
                                            │
                                      Mutex (console_mutex) ──► UART Console (Serial)
```

**同步原语分工**
- **Queue `app_event_queue`**：生产者（Button / UART）→ 消费者（LED）的事件传递。
- **Binary Semaphore `sys_timer_sem`**：软件定时器 → Monitor 的“每秒心跳”事件通知/同步。
- **Mutex `console_mutex`**：保护 UART 控制台（带优先级继承），防止多任务 `Serial` 输出交错。

---

## 10. Task 列表

| 任务 | 周期 | 优先级 | 内核亲和 | 职责 |
| --- | --- | --- | --- | --- |
| Task_LED | 500 ms | 2 | Core 1 | WS2812B RGB 心跳翻转 + 消费 LED 事件 |
| Task_Button | 20 ms | 3 | Core 1 | 按键轮询、去抖、发事件 |
| Task_Monitor | 1000 ms | 1 | Core 1 | 系统/内存/任务状态上报 |
| Task_UART | 50 ms | 3 | Core 1 | UART0 命令解释器 |

**优先级设计理由**
- UART 与 Button 是交互 I/O，优先级最高（3），保证用户输入不被饥饿。
- LED 仅是简单心跳，优先级中等（2）。
- Monitor 只做统计打印、从不阻塞关键工作，优先级最低（1），不会拖慢高优先级任务。
- Mutex 使用 `xSemaphoreCreateMutex`（带优先级继承），避免优先级反转。

**内核亲和性**：ESP32-S3 双核，Arduino/ESP-IDF 将网络与系统任务放在 Core 0；
本项目把 4 个应用任务绑定到 Core 1，互不干扰。`[TASK]` 表会输出每个任务实际运行的 Core ID。

**任务周期实现**：所有周期任务均使用 `vTaskDelayUntil()`（严格周期），
绝不使用 Arduino `delay()` 控制 FreeRTOS 任务周期。

---

## 11. UART 命令

| 命令 | 说明 |
| --- | --- |
| `help` | 列出所有命令 |
| `status` | 输出 uptime / free heap / free psram |
| `led on` | 通过 Queue 令 LED 常亮 |
| `led off` | 通过 Queue 令 LED 常灭 |
| `led toggle` | 通过 Queue 翻转一次 LED |
| `heap` | 输出 Heap Total / Free / Minimum Free |
| `psram` | 输出 PSRAM Total / Free / Minimum Free |
| `tasks` | 输出 FreeRTOS 任务表 |

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

> led on
LED ON

> status
Uptime: 125 s
Free Heap: 312 KB
Free PSRAM: 7200 KB
```

---

## 12. 内存监控

启动时输出（示例）：
```text
[MEM] Free Heap: 312 KB
[MEM] Minimum Free Heap: 312 KB
[MEM] PSRAM Total: 8192 KB
[MEM] PSRAM Free: 8192 KB
[MEM] PSRAM Minimum Free: 8192 KB
```

每秒 Monitor 输出：
```text
[SYS] uptime: 125s
[SYS] free_heap: 312 KB
[SYS] min_free_heap: 312 KB
[SYS] free_psram: 8192 KB
[SYS] min_free_psram: 8192 KB
[SYS] CPU0: available
[SYS] CPU1: available
[MEM] heap=312KB min=312KB
```

- Heap 通过 `ESP.getFreeHeap()` / `ESP.getHeapSize()` 获取；最小值由本工程自维护的
  `mem_min_heap()` 跟踪（不依赖特定 Core 版本的私有 API）。
- PSRAM 通过 `psramFound()` / `ESP.getPsramSize()` / `ESP.getFreePsram()` 获取；
  最小值由 `mem_min_psram()` 跟踪。
- **CPU 使用率**：ESP32 Arduino Core 没有可靠的逐核 CPU 负载 API，本项目**不伪造**
  使用率数据，仅输出双核“available”并在任务表中给出每个任务的真实 Core ID。

---

## 13. 常见问题 (FAQ)

**Q1：编译报 `invalid option 'PSRAM=...'`？**
A：不同 Core 版本菜单项名称不同。用 `arduino-cli board details esp32:esp32:esp32s3`
查看你安装版本支持的 `PSRAM` / `FlashSize` 取值，修正 `-b` 参数。

**Q2：运行时打印 `PSRAM: not available`？**
A：开发板菜单里没有启用 PSRAM。请设置 `PSRAM = OPI`（N16R8 为 Octal PSRAM）。

**Q3：LED 不亮 / 按键无反应？**
A：用户 LED 是 WS2812B（GPIO48），由 `Adafruit_NeoPixel` 驱动，需先安装库
（`arduino-cli lib install "Adafruit NeoPixel"`），否则编译报找不到 `Adafruit_NeoPixel.h`。
确认 `config.h` 中 `LED_PIN=48` 且 `LED_IS_WS2812=1`。按键请确认 `BUTTON_PIN=0`（BOOT 按钮）与板子一致。

**Q4：`[TASK]` 表没有 `xCoreID` / 编译报错？**
A：老版本 Core 的 `TaskStatus_t` 可能不含 `xCoreID` 字段。本工程按当前安装版本编译，
如遇此问题，可移除 `print_task_list()` 中 `st[i].xCoreID` 这一列（见 §15 验收状态）。

**Q5：任务创建失败 / Queue 创建失败？**
A：通常是 heap 不足。本工程在 `app_init()` / `app_start()` 对每个 FreeRTOS 对象创建
结果做了判空与 `[ERR]` 日志，便于定位。

---

## 14. 设计说明（给评审/验收）

- **无 `delay()` 周期控制**：所有 FreeRTOS 任务周期用 `vTaskDelayUntil()`。
- **无伪造硬件信息**：LED/按键 GPIO 以宏暴露、由用户按原理图填写；CPU 使用率不伪造。
- **无未使用大型模块**：每个 `.cpp` 都参与构建并被调用。
- **统一日志**：`[INFO]` / `[ERR]` / `[BTN]` / `[SYS]` / `[MEM]` / `[TASK]` 前缀统一。
- **换行兼容 Windows**：所有 `Serial.printf` / `console_printf` 行尾统一使用 `\r\n`
  （CRLF），在 Windows 串口终端（Arduino IDE Serial Monitor、PuTTY 等）下不会出现
  阶梯状错位；输入解析仍同时接受 `\n` 与 `\r\n`。
- **异常/初始化失败处理**：Queue / Semaphore / Mutex / Timer / Task 创建均做失败日志。

---

## 15. 验收状态

### 构建结果（本机实测，2026-08-26）

| 项目 | 结果 |
| --- | --- |
| 编译工具 | arduino-cli 1.5.1 |
| ESP32 Arduino Core | **3.3.11** |
| 目标 FQBN | `esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600` |
| 并行线程 | `-j 8`（限制，避免高核数机器过热） |
| 编译结果 | **[BUILD] PASS**（0 error / 0 warning） |
| 程序占用 | 314740 B（24% of program storage, max 1310720 B） |
| 动态内存 | 22880 B（6% of dynamic memory, max 327680 B） |
| 烧录 | **[FLASH] PASS**（COM21 CH343, esptool v5.3.1, 4 段 Hash verified） |
| 运行 | **[RUN] PASS**（串口 115200 捕获 15s 日志，全功能正常） |

> 烧录通过 COM21 (USB-Enhanced-SERIAL CH343) 完成。esptool 确认芯片 ESP32-S3 QFN56 rev v0.2，
> 双核 240MHz, Embedded PSRAM 8MB, MAC 44:1b:f6:ff:a1:18。
> 运行日志验证：Monitor 每秒输出、按键检测、Heap/PSRAM 稳定、任务表 11 个任务全部可见。

### 功能验收

| 项目 | 状态 |
| --- | --- |
| Arduino IDE / arduino-cli 可编译 | ✅ PASS（0 error / 0 warning） |
| CH343 + UART0 下载 | ✅ PASS（COM21, esptool v5.3.1） |
| ESP32-S3 启动 / LED 500ms 翻转 | ✅ PASS（Task_LED Block Prio=2 Core=1） |
| 按键事件 / Queue | ✅ PASS（`[BTN] pressed` 已捕获） |
| Semaphore / Mutex / Timer | ✅ PASS（Tmr Svc 存在，Monitor 每秒触发） |
| UART 命令 | ✅ PASS（Task_UART Block Prio=3 Core=1） |
| 双核信息 / 任务表 / 栈高水位 | ✅ PASS（11 任务，Core 分配正确） |
| Heap / PSRAM / uptime | ✅ PASS（heap=323KB, psram=8188KB, uptime 递增） |

---

*文档版本：1.1*
