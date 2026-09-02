---
name: esp32-arduino-cli-build
description: ESP32（S3/C3/C6）Arduino CLI 工程的构建、烧录、库管理与环境踩坑速查：单一编译单元范式（根 .ino 同名 + #include "xxx.cpp"）、头文件必须用 #ifndef 守卫、core 3.3.x 新 LEDC API、SERIAL_xNy 宏位置、arduino-cli 路径回退与端口扫描配方。适用于"用 arduino-cli 编译 ESP32""arduino-cli 报 main file missing""子目录 cpp 没被编译""ledcSetup 不存在""双击 bat 找不到 COM"等场景。
agent_created: true
---

# ESP32 + Arduino CLI 构建速查

## 1. 构建范式（arduino-cli 与 CMake 完全不同，务必先遵守）

| 规则 | 说明 | 违反后果 |
|------|------|----------|
| 根 `.ino` 文件名 **必须** 与工程目录同名 | `202.esp32s3_hw_detect/202.esp32s3_hw_detect.ino` | `main file missing from sketch` |
| 子目录 `.cpp` **不会**被自动编译 | 在 `.ino` 里统一 `#include "xxx.cpp"` 拼成**单一编译单元** | 链接期 `undefined reference` |
| 头文件用 `#ifndef/#define/#endif` | arduino-cli 会复制工程到 `.build/sketch/` 用不同路径解析，`#pragma once` 失效 | `redefinition of class/enum` |
| 每个 `.h` 自带它需要的所有 include | 单编译单元下 include 顺序敏感，靠间接包含必翻车 | `'xxx' was not declared` |
| 所有符号加命名空间/类作用域 | 单编译单元无 TU 隔离 | 静态变量跨模块串味 |

常用命令：
```bash
arduino-cli compile -j 8 -b "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600" --build-path ".build" .
arduino-cli upload  -b "<FQBN>" -p COM22 --build-path ".build" .
```
- 保留 `.build/` 即可**增量编译**。arduino-cli ≥1.5 **已移除 `--build-cache-path`**（会警告并忽略，无等价配置键）。
- 首次编译全量约 6–7 分钟；增量十几秒。

## 2. 环境与依赖

- `arduino-cli` **不一定在 PATH**。bat 里做多级回退，实测路径：
  `E:\agent-tools\arduino-cli_1.5.2-rc.1_Windows_64bit`（另有 `D:\software\arduino-cli`、`D:\data\agent-tools`）。
  先 `where arduino-cli`，失败再逐个试目录，再失败给明确提示——**不要**静默退出。
- 库安装报 `Download failed: performing HEAD request: ... EOF` 是**偶发网络抖动**，不是仓库问题。
  **逐个重试**即可通过，不要批量一次性装完就放弃。
  ```bash
  arduino-cli lib install ArduinoJson      # 7.4.3
  arduino-cli lib install PubSubClient     # 2.8.0
  arduino-cli lib install WebSockets       # 2.7.2
  arduino-cli lib install "Adafruit NeoPixel"  # 1.15.x
  arduino-cli lib list                     # 装完务必核对
  ```
- PlatformIO 在本机会被 `genie-trash` 守护锁 `.platformio` 导致 `packages.lock` 权限失败 / 解包死循环
  （卡 40 分钟无进展）。**ESP32 工程统一走 arduino-cli。**

## 3. core 3.3.x API 变更（高频踩坑）

| 旧 API（已移除） | 新 API |
|---|---|
| `ledcSetup(ch, freq, res)` | `ledcAttach(pin, freq, res)` |
| `ledcAttachPin(pin, ch)` | （同上，合并） |
| `ledcDetachPin(pin)` | `ledcDetach(pin)` |
| `ledcWrite(ch, duty)` | `ledcWrite(pin, duty)`（按引脚，不按通道） |

- `SERIAL_5N1 .. SERIAL_8O2` 宏定义在 **`cores/esp32/HardwareSerial.h` 第 61–79 行**（是 enum 值如 `0x8000010`），
  **不在** `esp32-hal-uart.h`。用 `HardwareSerial.begin(baud, config, rx, tx)` 直接传即可。
- 字面量 `0` 在多重载 API 下会歧义 → 显式强转：
  `ledcWrite(pin, (uint32_t)0)`、`strip.setPixelColor(0, (uint32_t)0)`。
- `analogSetPinAttenuation()` / `analogReadMilliVolts()` 存在于 core 3.3.x，可直接用（内部 ADC1 回退路径）。

## 4. 一键脚本（.bat）铁律

- **纯英文**（禁中文注释），LF 无 BOM，避免 GBK 控制台解析乱码。
- `cd /d "%~dp0"` 前先去掉尾随反斜杠。
- **for 块内禁止 `echo` 外部数据**（设备描述里的 `)` 会提前闭合 for 块 → 扫描逻辑静默崩掉）。
  把行解析抽成 `:subroutine`，在**块外**延迟展开打印。
- for 块内 `2>&1` 要写成 `> nul 2>nul`。
- **`echo`/`REM` 行清洗 `& ( ) | < >`**。

## 5. 端口扫描配方（flash 脚本）

双击"无反应"几乎都是**停在 `set /p` 等手输端口 + 日志被 `> log 2>&1` 吞屏**。
修法：双击 = 全自动识别，上传日志 `> log` 后 `type log` 回显。

```
主检测：arduino-cli board list
  - 按行首 ^COM[0-9] 过滤（多 FQBN 会有缩进续行）
  - 匹配 esp32:esp32（实测 ESP32-S3 的 FQBN 是 esp32:esp32:esp32_family，
    按 esp32s3 精确匹配永假！）
兜底：pyserial 扫 comports() 按 VID 303A（Espressif）
  - 先 python -c "import serial" 预检：双击时 where python 可能命中
    WindowsApps 存根，pyserial 会静默失效
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

## 7. 相关技能与工具

- **`esp32-web-ui-state-push`**（互补）：本技能管"构建/烧录/库/环境"，那个技能管"运行时网页 + 无硬件前端验证"。
  做带网页的 ESP32 固件时两个一起看。
- **全仓未跟踪审计**：`audit_untracked.py`（位于 `202.esp32s3_hw_detect/tools/`）按风险分级扫描
  "文件在磁盘但不在版本库"的目录，复跑：`python 202.esp32s3_hw_detect/tools/audit_untracked.py .`
- STM32 系列技能（`stm32-*`）与本技能平台不同，勿混用；但"事件总线解耦""零警告验收"等
  方法论可平移，见 `stm32-verification-acceptance` / `stm32-vibe-coding-workflow`。
