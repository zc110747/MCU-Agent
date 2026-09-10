---
name: esp32-arduino-cli-build
description: ESP32（S3/C3/C6）Arduino CLI 工程的构建、烧录、库管理、VS Code 调试与环境踩坑速查：单一编译单元范式（根 .ino 同名 + #include "xxx.cpp"）、头文件必须用 #ifndef 守卫、FQBN、core 3.3.x 新 LEDC API、启动早期读 MAC 必须走 eFuse API、SERIAL_xNy 宏位置、arduino-cli 路径三级回退与端口扫描配方、gitignore 目录黑名单、常见编译错误速修表。适用于"用 arduino-cli 编译 ESP32""arduino-cli 报 main file missing""子目录 cpp 没被编译""ledcSetup 不存在""双击 bat 找不到 COM""ESP32 VS Code Cortex-Debug 配置""AP 名出现 wifi-0000"。
agent_created: true
---

# ESP32 + Arduino CLI 构建速查

当 ESP32 工程用 **Arduino CLI + VS Code**（而非 PlatformIO / ESP-IDF）构建模块化 C++ 代码时，
必须处理 Arduino 构建模型的两个特殊约束。本 skill 沉淀 **201 / 202 / 203** 项目验证过的配方。

## 何时使用
- 新建 ESP32 Arduino 多文件工程，想保持 `app/ network/ bsp/ ...` 模块化目录。
- 从 PlatformIO 迁移（本机 `.platformio` 被 `genie-trash` 守护锁定时，`packages.lock` 权限失败 +
  框架解包死循环 → 改用 Arduino CLI 的未保护 `D:\software\arduino-cli\data`）。
- 工程报 `main file missing`、`redefinition of 'xxx'`、`#pragma once` 失效类错误。

## 1. 构建范式（arduino-cli 与 CMake 完全不同，务必先遵守）

| 规则 | 说明 | 违反后果 |
|------|------|----------|
| 根 `.ino` 文件名 **必须** 与工程目录同名 | `202.esp32s3_hw_detect/202.esp32s3_hw_detect.ino` | `main file missing from sketch` |
| 子目录 `.cpp` **不会**被自动编译 | 在 `.ino` 里统一 `#include "xxx.cpp"` 拼成**单一编译单元** | 链接期 `undefined reference` |
| 头文件用 `#ifndef/#define/#endif` | arduino-cli 会复制工程到 `.build/sketch/` 用不同路径解析，`#pragma once` 失效 | `redefinition of class/enum` |
| 每个 `.h` 自带它需要的所有 include | 单编译单元下 include 顺序敏感，靠间接包含必翻车 | `'xxx' was not declared` |
| 所有符号加命名空间/类作用域 | 单编译单元无 TU 隔离 | 静态变量跨模块串味 |

### 1.1 根 `.ino` 同名
arduino-cli 要求 `sketch/<DirName>/<DirName>.ino`，否则报
`Can't open sketch: main file missing from sketch`。
VS Code `launch.json` 的 `executable` 指向 **`.build/<DirName>.ino.elf`**（见第 7 节）。

### 1.2 单一编译单元写法
Arduino builder 只编译 `.ino` + 顶层文件，**不递归编译子目录 `.cpp`**。
保持模块化的解法：在 `.ino` 里**强制 `#include` 每个模块 `.cpp`**，把整个工程拼成一个编译单元。
```cpp
#include "app/debug_gateway.h"
#include "app/event_bus.cpp"
#include "app/pin_manager.cpp"
#include "app/debug_gateway.cpp"
#include "network/wifi_manager.cpp"
// ... 其余模块 .cpp
void setup() { /* 启动 */ }
void loop() { delay(1000); }
```
> 头文件照常用 `#include "xxx.h"`；`.cpp` 用 `#include "xxx.cpp"`。

### 1.3 `#ifndef` 守卫写法
```cpp
#ifndef APP_DEBUG_GATEWAY_H_
#define APP_DEBUG_GATEWAY_H_
// ...
#endif // APP_DEBUG_GATEWAY_H_
```
守卫宏 = 相对路径大写 + 下划线（如 `NETWORK_WIFI_MANAGER_H_`、`CONFIG_PIN_CONFIG_H_`）。

### 1.4 常用命令与 FQBN（N16R8 板）
```
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600
```
```bash
arduino-cli compile -j 8 -b "<FQBN>" --build-path ".build" .
arduino-cli upload  -b "<FQBN>" -p COM22 --build-path ".build" .
```
- ESP32 Arduino Core 3.3.11 已装于 `D:\software\arduino-cli\data\packages\esp32`。
- **增量编译**：core/库缓存在 arduino-cli data 目录自动生效；只要**保留 `--build-path .build/` 不删**，
  改动文件即可增量（首次全量约 6–7 分钟，增量十几秒）。
- arduino-cli ≥1.5 **已移除 `--build-cache-path`**（会警告并忽略，`config dump` 无等价配置键）；
  一键脚本里**不要 `rm -rf .build`**，需要干净构建时才手动清。

## 2. 环境与依赖

- `arduino-cli` **不一定在 PATH**。bat 里做多级回退，实测路径：
  `E:\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit`、`D:\data\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit`
  （另有 `D:\software\arduino-cli`）。
  先 `where arduino-cli`，失败再逐个试目录，再失败给明确提示——**不要**静默退出。
- 外部库（核心不自带，需 `arduino-cli lib install`）：

  | 库 | 版本 |
  |----|------|
  | ArduinoJson | 7.4.3 |
  | PubSubClient | 2.8.0 |
  | WebSockets | 2.7.2 |
  | Adafruit NeoPixel | 1.15.x（WS2812 状态灯，**RMT 驱动，勿用 digitalWrite**） |

  幂等安装（可放一键脚本首跑）：
  `arduino-cli lib install ArduinoJson PubSubClient WebSockets "Adafruit NeoPixel"`
  → 装入 `<arduino-cli data>\sketchbook\libraries`，装完必须 `arduino-cli lib list` 核对。
- 库安装报 `Download failed: performing HEAD request: ... EOF` 是**偶发网络抖动**，不是仓库问题。
  **逐个重试**即可通过，不要批量一次性装完就放弃。
- **以下均是核心自带，勿当外部库安装**：
  WiFi / WebServer / Preferences / HardwareSerial / Wire / Update / FreeRTOS。
- PlatformIO 在本机会被 `genie-trash` 守护锁 `.platformio` 导致 `packages.lock` 权限失败 / 解包死循环
  （卡 40 分钟无进展）。**ESP32 工程统一走 arduino-cli。**

## 3. core 3.3.x API 变更（高频踩坑）

| 旧 API（已移除） | 新 API |
|---|---|
| `ledcSetup(ch, freq, res)` | `ledcAttach(pin, freq, res)` |
| `ledcAttachPin(pin, ch)` | （同上，合并） |
| `ledcDetachPin(pin)` | `ledcDetach(pin)` |
| `ledcWrite(ch, duty)` | `ledcWrite(pin, duty)`（按引脚，不按通道） |

```cpp
ledcAttach(pin, freq_hz, res_bits);   // 替代 ledcSetup + ledcAttachPin
ledcWrite(pin, duty);
ledcDetach(pin);
```
编译报 `'ledcSetup' was not declared in this scope` 即此因。

- `SERIAL_5N1 .. SERIAL_8O2` 宏定义在 **`cores/esp32/HardwareSerial.h` 第 61–79 行**（是 enum 值如 `0x8000010`），
  **不在** `esp32-hal-uart.h`。用 `HardwareSerial.begin(baud, config, rx, tx)` 直接传即可。
- 字面量 `0` 在多重载 API 下会歧义 → 显式强转：
  `ledcWrite(pin, (uint32_t)0)`、`strip.setPixelColor(0, (uint32_t)0)`。
- `analogSetPinAttenuation()` / `analogReadMilliVolts()` 存在于 core 3.3.x，可直接用（内部 ADC1 回退路径）。
- **启动早期读 MAC → 必须用 eFuse API**：`WiFi.macAddress()` / `WiFi.softAPmacAddress()` 在 WiFi 驱动
  初始化前返回**全 0**（实测先 `WiFi.mode(WIFI_MODE_AP)` 再读 softAPmacAddress 仍全 0，开机仅 ~200ms 处）。
  正确做法：`esp_read_mac(mac, ESP_MAC_WIFI_STA)`（读 eFuse，无需 WiFi 启动），需 `#include <esp_mac.h>`。
  器件 ID 与 SSID 尾部宜同源（如 Device ID `esp32s3-F6FFA118` → AP 名 `wifi-A118`）。
  **症状**：AP SSID 变成 `wifi-0000` / 设备名全 0 → 一律先怀疑"驱动未起就读 MAC"。

## 4. 一键脚本（.bat）铁律

- **纯英文**（禁中文注释），LF 无 BOM，避免 GBK 控制台解析乱码。
- `cd /d "%~dp0"` 前先去掉尾随反斜杠。
- **for 块内禁止 `echo` 外部数据**（设备描述里的 `)` 会提前闭合 for 块 → 扫描逻辑静默崩掉）。
  把行解析抽成 `:subroutine`，在**块外**延迟展开打印。
- for 块内 `2>&1` 要写成 `> nul 2>nul`。
- **`echo`/`REM` 行清洗 `& ( ) | < >`**。

```bat
@echo off
where arduino-cli >nul 2>nul || (echo arduino-cli not on PATH & pause & exit /b 1)
arduino-cli lib install ArduinoJson PubSubClient WebSockets
arduino-cli compile -j 8 -b "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600" --build-path ".build" "%~dp0."
```
失败即 `pause`（英文输出）。

### 4.1 后台编译超时提醒约定（必须遵守）
- 后台运行 `arduino-cli compile` **超过 5 分钟**时，必须**主动提醒用户**当前编译仍在进行，
  并**询问是否终止**（本机全量构建正常 5~10 分钟，增量 1~3 分钟；超 5 分钟大概率是缓存重建）。
- 提醒内容附带：已运行时长、是否增量/全量、可用任务终止手段。
- 用户同意终止后，`.build/` **保留不清除**，下次增量续编。
- 同样适用于其他长命令（烧录 >2 分钟、构建脚本等）：**超时主动上报 + 询问，不要静默死等**。

### 4.2 .bat 诊断姿势（本机实测）
- **不要用 Git Bash 的 `cmd //c x.bat` 跑 .bat 诊断**：会进 cmd 交互模式（打印版本横幅停在提示符）。
- 正确做法：原生 PowerShell 落文件再读
  `& .\x.bat --no-pause 2>&1 | Out-File -FilePath out.txt -Encoding utf8`
  （PowerShell 的 stdout 在本环境不回显）。详见 `soc-debug-verification`。

## 5. 端口扫描配方（flash 脚本）

双击"无反应"几乎都是**停在 `set /p` 等手输端口 + 日志被 `> log 2>&1` 吞屏**。
修法：双击 = 全自动识别，上传日志 `> log` 后 `type log` 回显。

```
主检测：arduino-cli board list
  1. 按行首 ^COM[0-9] 过滤（多识别配置的板子会有缩进续行，首列为空/Serial）
  2. ESP 端口按 esp32:esp32 匹配（实测 S3 的 FQBN 是 esp32:esp32:esp32_family，
     按 esp32s3 精确匹配永假！）
  3. 设备描述含 "(" ")"（如 Serial Port (USB)）→ for 块内 echo 会提前闭合，
     必须抽 :scan_line / :py_line 子例程，列表在块外用 !VAR! 打印
兜底：pyserial 扫 comports() 按 VID 303A（Espressif）
  - 先 python -c "import serial" 预检：双击时 where python 可能命中
    WindowsApps 存根，pyserial 会静默失效
选择策略：唯一命中 → 直接烧录；多个/零个命中 → 才回退 set /p 让用户选
烧录：arduino-cli upload -p %PORT% -b "%FQBN%" --build-path "%BUILD%"，事后 DTR/RTS 复位脉冲
```

## 6. 【血泪教训】新建目录前先查 gitignore，别用 STM32 的黑名单目录名

真实事故：ESP32 工程把硬件驱动放在 `debug/`，磁盘丢失后发现**从未被 git 跟踪**——
最初误判为"漏了 `git add`"，真正根因是仓库根的 `.gitignore`（为一批 STM32 工程写的）：

```gitignore
**/Drivers        # 冲突：想要的分层名
**/third_party    # 冲突：想要的分层名
**/zephyr
**/Debug/*        # ← 真凶：Windows 大小写不敏感，debug/ 被当成 Debug/ 屏蔽
**/Release/*
**/obj/*
**/.build
```

`git add` 会**静默跳过**被忽略的文件，不报错、不提示——所以"我明明 add 过了"是幻觉。

**铁律：建新目录前先跑一次探针**
```bash
git check-ignore -v <工程>/<新目录>/probe.txt
# 有输出 = 被屏蔽，换名；无输出 = 安全
```

**已知黑名单（多工程仓库，勿用）**：
`Drivers` / `third_party` / `zephyr` / `Debug`（**含小写 `debug`**）/ `Release` / `obj` / `.build`

安全替代：硬件驱动用 **`bsp/`**（语义也更贴切），第三方库用 `lib/` 或 `vendor/`。

若已踩坑，重建依据优先级：**调用点（.ino / gateway 里怎么调的） > 文档里的 API 与协议契约 > 记忆**。
因此文档里的 API 签名与 JSON 协议字段**必须写精确**，这是唯一可还原的凭据。

## 7. VS Code 调试配置 `.vscode/`

- `tasks.json`：`Build (arduino-cli)` 默认任务，等价第 1.4 节 compile 命令。
- `launch.json`：Cortex-Debug 配置（内置 JTAG / ESP-Prog / FTDI），
  `executable: ${workspaceFolder}\.build\<DirName>.ino.elf`。
- 调试工具链路径（202 复用 201）：`esp-x32/2601`、`xtensa-esp-elf-gdb/17.1_20260402`、
  `openocd-esp32/v0.12.0-esp32-20260424`。
- 注意：这是 **JTAG + GDB** 调试链路，与 STM32 的 ST-Link/OpenOCD 同源思路，
  详见 `esp32-cortex-debug`。

## 8. 常见编译错误 → 修复（202 实战清单）

| 报错 | 修复 |
|---|---|
| `constexpr IPAddress ... not literal` | 改 `const IPAddress`（IPAddress 非字面量类型） |
| `'JsonObjectConst'/'JsonDocument' does not name a type` | 缺 `#include <ArduinoJson.h>` |
| `'RESERVED_COUNT'/'RUN_LED' was not declared` | 头文件补 `#include "config/pin_config.h"` |
| `'WiFi' was not declared` | 头文件补 `#include <WiFi.h>` |
| `'ledcSetup' was not declared in this scope` | core 3.3.x 已移除，改用 `ledcAttach`（见第 3 节） |
| 自由函数回调访问 `_self`/`onMessage`/`onText` 报 private | 把这些静态成员/回调方法移到 `public` |
| `Update.write` const 转换错误 | `const_cast<uint8_t*>(data)` |
| 私有方法被 `.cpp` 先调用报 not declared | 在 `.h` 补私有声明 |

## 9. 验收

- `build_oneclick.bat` 退出码 0，**零错误零警告**。
- 内存实测（arduino-cli 输出）：记录 Flash/DRAM 占用与上限百分比，格式对齐 STM32 侧
  （如 `FLASH xxxB / 16MB (xx%)`）。
- 产物：`.build/<DirName>.ino.bin` / `.elf` / `.merged.bin` / `partitions.bin` / `bootloader.bin`。
- 真机功能验证（UART/ADC/GPIO/MQTT/WS/OTA）不阻塞编译交付，但应列入回归清单。

## 10. 相关技能与工具

- **`esp32-web-ui-state-push`**（互补）：本技能管"构建/烧录/库/环境"，那个技能管"运行时网页 + 无硬件前端验证"。
  做带网页的 ESP32 固件时两个一起看。
- **全仓未跟踪审计**：`audit_untracked.py`（位于 `202.esp32s3_hw_detect/tools/`）按风险分级扫描
  "文件在磁盘但不在版本库"的目录，复跑：`python 202.esp32s3_hw_detect/tools/audit_untracked.py .`
- STM32 系列技能（`stm32-*`）与本技能平台不同，勿混用；但"事件总线解耦""零警告验收"等
  方法论可平移，见 `stm32-verification-acceptance` / `stm32-vibe-coding-workflow`。
