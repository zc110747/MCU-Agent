# ESP32-S3 Remote Hardware Debugger — 项目开发记录

> 把一个 ESP32-S3（N16R8）变成**网络化硬件调试网关**：实时把目标 MCU 的
> **UART 日志 / 外部 ADC 电压 / GPIO 状态** 通过 **Web(WebSocket) + MQTT**
> 暴露给浏览器、上位机与未来 AI Agent。
>
> 本文件是**开发过程记录**（需求来源、架构决策、踩坑与解决、当前状态）。
> - 操作与硬件接线见 **`user_document.md`**
> - 完整交付与协议细节见 **`DELIVERY.md`**

---

## 0. 需求来源与功能范围

原始需求为一份 37 节规格：模块化设计、统一事件总线、UART/ADC/GPIO 监视、
WiFi STA+AP、MQTT、Web+WebSocket、OTA、配置持久化、GPIO 资源管理等。
核心设计约束（用户确认）：**GPIO48 为板载 WS2812B 状态灯，由专用 LED 控制器独占**，
支持 off/R/G/B 单色闪烁/RGB 循环，禁止被通用 GPIO 模块占用；19/20（USB D-/D+）永久保留。

最终交付的能力：

| 能力 | 说明 |
|------|------|
| UART 监视 | `HardwareSerial(1)`，默认 115200/8N1，运行时可改波特/数据位/停止位/校验；文本/HEX 双模式 |
| ADC（外部+内部回退） | ADS1115（I2C）4 通道 ±6.144V；**未接 ADS1115 时自动回退 ESP32 内部 ADC（GPIO1/2）** |
| GPIO 监视 | 4 路（GPIO4/5/6/7），默认输入上拉，可经 Web/MQTT 切输出 |
| WS2812 状态灯 | GPIO48 板载 WS2812B，Adafruit_NeoPixel（RMT）驱动；off/r/g/b/cycle 五种模式，Web + `ws2812_set` 命令 + `led` 事件 |
| PWM 输出 | LEDC 单路输出，Web 可选引脚/周期/占空比（core 3.3.11 新 API `ledcAttach/ledcWrite/ledcDetach`），`pwm_set` 命令 + `pwm` 事件 |
| WiFi | STA + AP 回退，自动重连 |
| MQTT | PubSubClient 发布订阅 + 自动重连 + 命令转发 |
| Web/WS | 单页界面：**Interface**（GPIO + ADC 读值 + PWM + WS2812）与 **Hardware**（芯片/时钟/启用硬件/Web/MQTT 地址端口，原 Dashboard+System 合并）+ REST API + WebSocket 实时推送（端口 80 / 81）。**界面零轮询**：设备每 400 ms 主动推 `state` 快照驱动 Interface 全部读数 |
| OTA | Web OTA（仅写 app 分区，保护 NVS） |
| 配置持久化 | Preferences(NVS)：WiFi / MQTT / UART / ADC / GPIO 方向 / 设备 ID |
| GPIO 资源管理 | PinManager 运行时强制拒绝 19/20（USB D-/D+）；GPIO48 由 LED 控制器 `claim(48,"LED")` 独占 |

---

## 1. 架构决策

### 1.1 统一 Event Bus（解耦核心）
生产者（UART/ADC/GPIO）只负责把 `DebugEvent` 推入 FreeRTOS Queue；
单消费者 `EventTask` 把事件扇出到 WebSocket / MQTT / Log，并每 5s 广播
SYSTEM 状态。`push()` 用 `xQueueSend(...,0)` **永不阻塞**，队列满则丢弃并
计数，保证慢网络客户端不会拖垮 UART 接收。新增模块（CAN/I2C/SPI/逻辑分析仪）
只需实现 `DebugModule`，核心零改动。

### 1.2 模块化目录布局
`app/ config/ network/ bsp/ storage/ ota/`，每个功能一个子模块，
头文件集中声明、`.cpp` 落地实现，符合「禁止把所有代码塞进 main」的约束。

### 1.3 Arduino 构建范式（关键）
选定 **Arduino CLI + VS Code**（详见第 2 节），其构建模型要求：
- 根目录 `.ino` 文件名**必须**与工程目录同名（`202.esp32s3_hw_detect.ino`），
  否则 arduino-cli 报 `main file missing`。
- arduino-cli **不会自动编译子目录里的 `.cpp`**。工程用 `.ino` 中统一
  `#include "xxx.cpp"` 把所有模块拼成**单一编译单元**（与 201 项目一致的范式）。
- 全部 19 个头文件使用传统 `#ifndef/#define/#endif` 守卫——arduino-cli 会把
  工程复制到 `.build/sketch/` 用不同路径解析，`#pragma once` 会失效导致重定义。

---

## 2. 开发历程（踩坑与解决）

### Phase 1 — PlatformIO 初版：环境阻塞
最初用 PlatformIO 搭建，但本机 `.platformio` 目录被 **`genie-trash` 守护锁定**，
导致 `packages.lock` 权限失败与框架解包死循环；一次 `pio run` 卡了 40 分钟无进展。
根因是**环境问题**（受保护目录），不是代码错误。

### Phase 2 — 决策：切换到 Arduino CLI + VS Code
用户明确指示「使用 Arduino CLI + VS Code 的方案，提示词也确定使用 Arduino」。
Arduino CLI 使用独立且**未受保护**的 `D:\software\arduino-cli\data`，
esp32 core 3.3.11 已就绪，编译干净通过——故从 PlatformIO 迁移。

### Phase 3 — 工程重构
- 删除 `src/main.cpp`、`src/`、`platformio.ini`、`.pio` 等；
- 原 `src/*` 拍平为 `app/ config/ network/ bsp/ storage/ ota/`；
- 新建 `202.esp32s3_hw_detect.ino` 作为强制 `#include` 入口。

### Phase 4 — 编译错误攻坚（全部修复，零警告）
| 现象 | 根因 | 修复 |
|------|------|------|
| `main file missing from sketch` | `.ino` 名 ≠ 目录名 | `git mv` 改名 + 更新 3 处 launch.json elf 路径 |
| `redefinition of enum/class` | `#pragma once` 在复制路径下失效 | 全 19 个头文件改 `#ifndef` 守卫 |
| `constexpr IPAddress not literal` | IPAddress 非字面量类型 | 改 `const IPAddress` |
| `'JsonObjectConst'/'JsonDocument' not declared` | 缺 `<ArduinoJson.h>` | 在 `debug_gateway.h`/`web_server.h` 补 include |
| `RESERVED_*` 未声明 | `pin_manager.h` 缺 `pin_config.h` | 补 include |
| `'WiFi' was not declared` | `wifi_manager.h` 缺 `<WiFi.h>` | 补 include |
| `'_self'/'onMessage'/'onText' is private` | 自由函数回调访问不到私有静态成员 | 移到 `public` |
| `Update.write` const 转换错误 | 形参为 `const uint8_t*` | `const_cast<uint8_t*>` |
| `esp_read_mac` 未声明 | 缺 `<esp_mac.h>` | 补 include |

### Phase 5 — 一键脚本（参考 201 项目）
- `build_oneclick.bat`：检查 arduino-cli → 幂等装库（ArduinoJson 7.4.3 /
  PubSubClient 2.8.0 / WebSockets 2.7.2）→ `arduino-cli compile -j 8 --build-path .build`。
- `flash-esp32.bat`：自动扫描 COM（**主：`arduino-cli board list` 识别 esp32 核心 FQBN；
  兜底：pyserial 按 VID `303A` 锁定 ESP32**）+ 烧录 + DTR/RTS 复位；支持 `COMx` / `monitor` / `--no-pause`。
- `.vscode/`：复用 201 调试工具链（esp-x32 / xtensa-esp-elf-gdb / openocd-esp32），
  3 套 Cortex-Debug 配置。

### Phase 6 — 烧录与"连接"体验修复
首次双击 `flash-esp32.bat` 表现为"无反应"：根因是旧脚本无参数时停在
`set /p "Select port number"` 等待手输端口，日志又被吞进文件。
排查确认编码一致、依赖齐全、板子在 `COM22`（VID `303A:1001`）后，重写为
**双击即自动识别 ESP32 端口一键烧录**，上传日志回显屏幕。

### Phase 7 — 真机验收脚本
新增 `tools/verify/*.py`（纯 stdlib、英文输出防 GBK 乱码、pass/fail 计数）：
`verify_web.py`（Web/鉴权/状态 API）、`verify_gpio.py`（读写 + PinManager 拒绝）、
`verify_adc.py`（采样端点）、`verify_ws.py`（WS 实时推送）、`run_all.py`（汇总）。
已通过 `py_compile` 语法体检。

### Phase 8 — 功能扩展：WS2812 / PWM / Web 重构 / 脚本健壮性
- **WS2812 状态灯（GPIO48）**：GPIO48 移出 `RESERVED_PINS`（19/20/48 → 19/20），
  由 `Ws2812Controller`（Adafruit_NeoPixel + RMT，`NEO_GRB+NEO_KHZ800`，亮度 40）`claim(48,"LED")`；
  FreeRTOS 任务 250ms 半周期翻转，支持 OFF/BLINK_R/BLINK_G/BLINK_B/CYCLE_RGB；
  全链路：`ws2812_set` 命令 → `LED_STATE` 事件 → WS `led` 推送 → 网页按钮高亮。
  完整构建通过：Flash 1,032,767 B（78%）/ DRAM 80,716 B（24%），零错误。
- **PWM 输出模块**（`bsp/pwm_output.h/.cpp`）：LEDC 单路，Web 选引脚/周期(µs)/占空比(%)，
  apply/stop；PinManager claim 引脚，换脚自动 detach+release 旧脚。
  **坑：core 3.3.11 移除旧 LEDC API**（`ledcSetup/ledcAttachPin/ledcDetachPin`），
  必须用新引脚式 API `ledcAttach/ledcWrite/ledcDetach`。
- **Web 页面重构**：GPIO 标签 → **Interface**（合并 ADC 读值表格+曲线、PWM 控制、WS2812 控制）；
  Dashboard+System 合并为 **Hardware** 页（芯片型号、主频、各模块启用状态、Web IP+端口、
  MQTT 地址+端口）；`system` WS 消息增补硬件字段，页面加载即渲染。
- **编译缓存**：arduino-cli 1.5.2 已**移除 `--build-cache-path`**（警告并忽略，无等价配置键）；
  core/库缓存自动保存在其 data 目录，只要**保留 `.build/` 不删**即可增量编译。`build_oneclick.bat`
  相应修正；`arduino-cli` 不在 PATH 时自动回退到本地安装路径。
- **flash-esp32.bat 端口扫描修复**（双击找不到 COM 的三个叠加根因）：
  1. `for` 块内 `echo %%C` 输出设备描述 `Serial Port (USB)` —— **块内 `)` 提前闭合 for 块**，
     扫描逻辑直接崩掉（.bat 经典坑，见 soc-debug-verification skill）；
  2. 板子实际被识别为 FQBN `esp32:esp32:esp32_family`，旧逻辑按 `esp32:esp32:esp32s3` 匹配永假；
  3. 双击时 `where python` 可能命中 WindowsApps 存根，pyserial 检测静默失效。
  修法：主检测改 `arduino-cli board list`（零额外依赖，按行首 `^COM[0-9]` 过滤多 FQBN 续行，
  按 `esp32:esp32` 匹配 ESP 端口）；pyserial 降为兜底且先 `python -c "import serial"` 预检；
  行解析与端口列表展示抽成 `:scan_line`/`:py_line` 子例程（块外延迟展开打印，规避括号坑）。

### Phase 9 — `bsp/` 模块重建 + WebSocket 状态推送（界面实时化）

**9.1 阻断性事故：硬件模块目录整体丢失**
开工时发现硬件模块目录（当时叫 `debug/`，见 Phase 10 改名）下 5 个模块（uart_monitor / adc_monitor /
gpio_monitor / ws2812_led / pwm_output，共 10 个文件）在磁盘上**完全不存在**，而 `.ino` 里
`#include "debug/*.cpp"` 全部指向它们——工程处于"根本无法编译"状态。排查：`git ls-files` 显示该目录下
文件**从未被跟踪**，全盘搜索 Temp / Downloads / D: / E:\cnb / .workbuddy 无任何副本。
> **教训**：代码"能跑"不等于"已提交"。模块化工程里新目录极易漏 `git add`，一旦本地丢失无法还原。
> 本次只能依据 `app/debug_gateway.cpp` 的调用点 + `DELIVERY.md`/`README.md`/`user_document.md`
> 的 API 与协议契约**逆向重建全部 10 个文件**，并保证每个被 gateway / web_server 调用的访问器签名一致。
> 注：当时误判为"漏 git add"，实际**真正根因是根目录 `.gitignore` 的 `**/Debug/*` 规则**——
> Windows 文件系统大小写不敏感，`debug/` 被当成 `Debug/` 屏蔽，连 `git add` 都会静默跳过。

重建要点（与原先设计保持一致，另补本次需求所需的访问器）：

| 模块 | 关键实现 |
|------|----------|
| `uart_monitor` | `HardwareSerial(1)`（RX=17/TX=18）+ 4096 B 环形缓冲；`buildConfig()` 把 5N1..8O2 映射为 `SERIAL_5N1..SERIAL_8O2`（宏在 `cores/esp32/HardwareSerial.h` 第 61-79 行）；文本模式按行成帧 + 30 ms idle flush，HEX 模式 16 字节成块 |
| `adc_monitor` | `ExternalAdc` 抽象基类 + `Ads1115Adc`（I2C 0x48，860 SPS，**轮询 OS 位而非盲延时**）+ `EspAdc`（内部 ADC1 GPIO1/2）；新增 `sampleOnce()` 与 `latest(raw[],volts[])` 缓存供状态快照读取（不额外占用 I2C） |
| `gpio_monitor` | 4 路 GPIO4..7，默认 `INPUT_PULLUP`，NVS 持久化方向；**写后必回读**再发布；新增 `states()/level()/isOutput()/outputMask()` |
| `ws2812_led` | Adafruit_NeoPixel(RMT) 驱动 GPIO48，250 ms 半周期状态机；除 `mode` 外维护实时输出 `_on/_r/_g/_b` 并序列化；新增静态 `modeName()/modeFromName()` |
| `pwm_output` | LEDC 单路，新 API `ledcAttach/ledcWrite/ledcDetach/ledcChangeFrequency`；`pickResolution(freq)` 按 `80MHz / 2^res >= freq` 从 12 bit 降到 8 bit；`configure/setDuty/stop` 后均 `publishState()` 报告**量化后的实际参数** |

**9.2 需求实现：WebSocket 服务器主动推送 `state` 快照**
用户四项需求（GPIO 电平真实反馈 / WS2812 去掉彩色改为显示实际工作状态 / PWM apply 后实时状态 /
ADC 实时更新）归结为同一个根因——**界面显示的是"命令回显"而不是"硬件状态"**。
解法不是给每个控件打补丁，而是建立统一的状态推送通道：

- `AppConfig::STATE_PUSH_INTERVAL_MS = 400`：ws_task 每 400 ms 广播一次完整 Interface 快照。
- `WebsocketManager` 增加 `onConnect/onDisconnect/hasClients/clientCount` 与私有 `broadcastState()`；
  新客户端连上时置 `_lastState = 0`，**立即**收到首帧；无客户端时零开销。
- **快照不进 EventBus**：放进 `DebugEvent` 会使其 `sizeof` 从 ~220 B 涨到 800 B+，深度 64 的队列多占约
  37 KB DRAM；改为 ws_task 内直接读模块缓存 + `broadcastTXT()`，DRAM 仅增约 276 B。
- `broadcastEvent()` 中 `LED_STATE` / `PWM_STATE` 合并为"data 已是完整 JSON，直接广播"。
- 前端 `web_pages.h` **全量重写**：Interface 页拆成 4 张卡片（GPIO Monitor / WS2812 Status LED /
  PWM Output / ADC Read），删除全部 `setInterval` 轮询与断线 `location.reload()`（改 3 s 自动重连）。
  所有控件由 `state` 驱动；WS2812 用中性描边（删掉原来的绿色高亮 `background:'#2a7'`）并显示
  `模式 | ON rgb(r,g,b)`；PWM 显示实际参数并回填输入框（聚焦中不覆盖）；ADC 曲线按峰值自适应缩放。
- `/api/gpio` GET 同步扩展 `led_state` / `pwm.freq` / 内联 `adc[]` + `adc_src`，保证首屏不空白。

**9.3 无硬件验证（Node 沙箱 + stdlib Python）**
无法连板时，用两个 Node 脚本把前端逻辑跑起来做行为断言：
- `tools/chk_pages.js`：抽出 `DASHBOARD_HTML` raw string，`new Function(code)` 校验 JS 语法，
  核对 52 个 DOM id 与 6 个 pane 均存在，并检查无内联彩色样式 → `ALL_IDS_OK(52) / ALL_PANES_OK /
  NO_INLINE_COLOR_OK(monochrome)`。
- `tools/chk_ui_logic.js`：`vm` 沙箱 + 最小 DOM 桩（含 `classList`、`insertRow/insertCell`、
  `canvas getContext` stub、`fetch`/`WebSocket` 桩），注入真实 `state` 消息后断言 22 项 UI 行为
  → **22/22 通过**。
- `tools/verify/verify_interface.py`（新增）：真机端到端，WS 下发命令 → 回读 `state` 断言四项需求。
- `tools/verify/rhd_common.py` 新增纯 stdlib 的 RFC 6455 客户端 `WSClient`（含 masking/ping-pong），
  `verify_ws.py` 改写为复用它并新增"快照含 gpio/led/pwm/adc 四段"断言。

**9.4 踩坑**
- 三个第三方库（ArduinoJson / PubSubClient / WebSockets）实际未安装，只剩 Adafruit NeoPixel；
  首次批量 `lib install` 报 `HEAD request: EOF`（网络抖动），**逐个重试**后全部装上
  （ArduinoJson 7.4.3 / PubSubClient 2.8.0 / WebSockets 2.7.2）。
- `arduino-cli` 实际在 `E:\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit`，与记忆里的
  `D:\data\agent-tools` 不符 → 给 `build_oneclick.bat` 增加 `E:\agent-tools` 兜底分支。
- `ledcWrite(pin, 0)` / `setPixelColor(0, 0)` 的字面量 `0` 在多重载下歧义 → 显式 `(uint32_t)0`。
- `chk_ui_logic.js` 报 `doc.getElementById('led_g').onclick is not a function`：DOM 桩里 `id` 是普通属性，
  动态 `createElement` 后赋 `b.id='led_g'` 不会注册进 `getElementById` 的索引 →
  把 `id` 改成 getter/setter，setter 中 `doc._byId[v] = this`，模拟真实 DOM 注册行为。
- Git Bash 下 node 读不到 `/tmp/chk.js`（`/tmp` 映射到 `E:\tmp` 但 node 按 Windows 路径解析失败）
  → 脚本一律落到工程内 `tools/` 并用相对路径运行。

### Phase 10 — `debug/` → `bsp/`：绕开 STM32 工程的 gitignore 屏蔽

**10.1 为什么必须改名（Phase 9 事故的真正根因）**
仓库根 `E:\cnb\git\MCU-Agent\.gitignore` 是给一批 **STM32 工程**写的，里面有几条与本工程冲突的规则：

```gitignore
**/Drivers        # 冲突：本工程想要的分层名
**/third_party    # 冲突：本工程想要的分层名
**/zephyr
**/Debug/*        # ← 真凶：Windows 大小写不敏感，debug/ 被当成 Debug/ 屏蔽
**/obj/*
**/.build         # 构建目录，本工程已在用，属预期
```

用 `git check-ignore -v` 验证：
```
$ git check-ignore -v 202.esp32s3_hw_detect/debug/adc_monitor.cpp
.gitignore:80:**/Debug/*	202.esp32s3_hw_detect/debug/adc_monitor.cpp
```
即 **`debug/` 下的文件根本进不了版本库**（`git add` 也会静默跳过），Phase 9 的"目录丢失无法还原"
正源于此——不是漏提交，是被忽略了。

**10.2 改名范围**
`debug/` → **`bsp/`**（板级支持包，语义也更贴切：这些模块就是硬件驱动）。
用户明确要求避开 `build` / `Drivers` / `debug` / `third_party` 四个名字。

| 类型 | 改动 |
|------|------|
| 目录 | `mv debug bsp` |
| `.ino` | 5 处 `#include "bsp/xxx.cpp"` + 顶部目录注释 |
| `app/debug_gateway.cpp` | 5 处 `#include "bsp/xxx.h"` |
| `network/websocket_manager.cpp` | 3 处 `#include "bsp/xxx.h"` |
| `bsp/*.cpp` | 4 处自包含头文件路径 |
| 文档 | `README.md` / `DELIVERY.md` / `user_document.md` / `prompter.md` 全部路径同步 |

`app/debug_gateway.*` 与 `DebugEvent` 等**标识符保持不变**——它们不是目录，不受 gitignore 影响，
改名只会徒增 diff。

> **跨项目铁律**：在这个多工程仓库里新建目录前，先跑
> `git check-ignore -v <工程>/<新目录>/probe.txt` 确认不被屏蔽。
> 已知黑名单：`Drivers` / `third_party` / `zephyr` / `Debug`(含小写 `debug`) / `Release` / `obj` / `.build`。

---

## 3. 当前状态（实测）

- **最近一次完整构建（bsp/ 重建 + WS state 推送版）**：退出码 0，零错误零警告（耗时 6m54s）。
  | 项 | 占用 | 上限 | 占比 |
  |----|------|------|------|
  | Flash | 1,053,526 B | 1,310,720 B | 80% |
  | DRAM  | 80,876 B   | 327,680 B   | 24% |
  （上一版 WS2812 + ADC 内部回退版为 Flash 1,032,767 B / 78%、DRAM 80,716 B / 24%；
  本版 +20,759 B 主要来自重建的 `bsp/` 模块与 `broadcastState()`。）
- **静态验收（无硬件）全部通过**：
  - `tools/chk_pages.js` → `JS_SYNTAX_OK`、`ALL_IDS_OK (52)`、`ALL_PANES_OK`、`NO_INLINE_COLOR_OK`
  - `tools/chk_ui_logic.js` → **22/22 passed, 0 failed**（四项需求的 UI 行为全覆盖）
  - `tools/verify/*.py` → `py_compile` 全部通过
- **flash-esp32.bat 端口扫描**：已修复三处根因（括号坑 / FQBN 匹配 / python 存根），
  解析逻辑经 `arduino-cli board list` 真实输出校验（COM22 → `esp32:esp32:esp32_family` 命中），
  端到端待双击验证。
- **待真机验证**（需烧录后跑 `tools/verify/run_all.py`）：WS2812 五模式实时输出、PWM 波形与量化参数、
  GPIO 回读电平、ADC 四通道、UART 端到端、MQTT、OTA。

---

## 4. 已知限制与后续

- MQTT TLS 字段已预留但 v1 仍明文 TCP；OTA 仅 Web；AI Agent 仅协议层就绪未实现逻辑。
- ADS1115 单拍 4 通道上限约 125 Hz；921600 超高速下 WS/MQTT 长期掉线会触发设计内 drop 保护。
- PWM 为单路输出（新配置自动释放旧引脚）；WS2812 亮度固定 40（防眩光）。
- **下一步**：烧录本版 → 跑 `cd tools\verify && python run_all.py`（AP 模式）做端到端回归 →
  按需恢复新需求。
- **`state` 快照周期**：目前固定 400 ms（`AppConfig::STATE_PUSH_INTERVAL_MS`）。若多客户端 + 高 UART
  吞吐下发现 ws_task 栈/带宽吃紧，可放宽到 500~1000 ms；界面观感 400 ms 已接近"即时"。
- 更远：安全层（TLS/HTTPS/JWT）、AI Agent 总线联动、UART DMA 双缓冲、配置版本号 + 工厂复位。

---

## 5. 文档索引

- `user_document.md` — **操作手册 + 硬件接线**（上手必读）
- `DELIVERY.md` — 完整交付文档（目录、协议、内存、限制、验收）
- `tools/verify/` — 真机回归自测脚本
