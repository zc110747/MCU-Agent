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
| Web/WS | 单页界面：**Interface**（GPIO + ADC 读值 + PWM + WS2812）与 **Hardware**（芯片/时钟/启用硬件/Web/MQTT 地址端口，原 Dashboard+System 合并）+ REST API + WebSocket 实时推送（端口 80 / 81） |
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
`app/ config/ network/ debug/ storage/ ota/`，每个功能一个子模块，
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
- 原 `src/*` 拍平为 `app/ config/ network/ debug/ storage/ ota/`；
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
- **PWM 输出模块**（`debug/pwm_output.h/.cpp`）：LEDC 单路，Web 选引脚/周期(µs)/占空比(%)，
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

---

## 3. 当前状态（实测）

- **最近一次完整构建（WS2812 + ADC 内部回退版）**：退出码 0，零错误零警告。
  | 项 | 占用 | 上限 | 占比 |
  |----|------|------|------|
  | Flash | 1,032,767 B | 1,310,720 B | 78% |
  | DRAM  | 80,716 B   | 327,680 B   | 24% |
- **已实现待编译验证**（用户自行编译，`.build/` 缓存保留可增量）：PWM 输出、
  Interface/Hardware 页面重构、`build_oneclick.bat` 缓存修正。
- **flash-esp32.bat 端口扫描**：已修复三处根因（括号坑 / FQBN 匹配 / python 存根），
  解析逻辑经 `arduino-cli board list` 真实输出校验（COM22 → `esp32:esp32:esp32_family` 命中），
  端到端待双击验证。
- **待真机验证**：WS2812 五模式、PWM 波形、Interface/Hardware 页面、UART/ADC/GPIO 端到端、
  MQTT、WS 推送、OTA——由 `tools/verify` 提供回归基线。

---

## 4. 已知限制与后续

- MQTT TLS 字段已预留但 v1 仍明文 TCP；OTA 仅 Web；AI Agent 仅协议层就绪未实现逻辑。
- ADS1115 单拍 4 通道上限约 125 Hz；921600 超高速下 WS/MQTT 长期掉线会触发设计内 drop 保护。
- PWM 为单路输出（新配置自动释放旧引脚）；WS2812 亮度固定 40（防眩光）。
- 下一步：编译 + 烧录 PWM/Interface/Hardware 版本 → 双击验证端口扫描 → WS2812/PWM 真机验收
  （可扩展 `tools/verify` 脚本）→ 按需恢复新需求。
- 更远：安全层（TLS/HTTPS/JWT）、AI Agent 总线联动、UART DMA 双缓冲、配置版本号 + 工厂复位。

---

## 5. 文档索引

- `user_document.md` — **操作手册 + 硬件接线**（上手必读）
- `DELIVERY.md` — 完整交付文档（目录、协议、内存、限制、验收）
- `tools/verify/` — 真机回归自测脚本
