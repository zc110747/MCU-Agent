---
name: esp32-board-hardware
description: >
  ESP32-S3 板级硬件（用户 LED / 按键 / 串口）与真机烧录 + 串口验证的完整配方。
  涵盖：板载 WS2812B RGB LED（GPIO48，GRB 800kHz，不能用 digitalWrite）的标准驱动方案
  （Adafruit_NeoPixel + RMT）、BOOT 按键（GPIO0 active-low）、板载 USB-Serial-JTAG
  烧录/调试口（VID 0x303a PID 0x1001）识别、flash-esp32.bat 烧录、pyserial 串口验证验收。
  适用于"ESP32 LED 不亮""WS2812 怎么驱动""ESP32 怎么烧录验证""GPIO48 WS2812"等请求。
agent_created: true
---

# ESP32-S3 板级硬件与真机验证配方

适用：ESP32-S3（N16R8：OPI PSRAM + 16MB Flash），Arduino Core `esp32:esp32@3.3.11`。
调试链路（openocd/gdb/launch.json）见配套 skill **`esp32-cortex-debug`**。

## 1. 板级硬件事实（本项目 201.esp32s3_rtos 实测）

| 元件 | 连接 | 软件可控 |
|------|------|---------|
| **用户 LED** | **WS2812B RGB，数据线 GPIO48**（单线 800kHz 协议，GRB 字节序） | ✅ 是（须专用驱动） |
| BOOT 按键 | GPIO0，R5(10k) 上拉到 VDD33，active-low | ✅ 是 |
| PWRLED-RED | +5V → R6(1k) → LED → GND（电源常亮） | ❌ 否 |
| TXLED2 | U0TXD(GPIO43) → R4(1k) → LED → GND（UART TX 活动） | ❌ 否 |
| RXLED2 | U0RXD(GPIO44) → R1(1k) → LED → GND（UART RX 活动） | ❌ 否 |

> ⚠️ 常见误区：很多 ESP32-S3 资料把"用户 LED"说成 GPIO2 普通数字脚，或 DevKitC-1 的
> GPIO48 是"WS2812 RGB 但别当数字脚用"。本项目板上 GPIO48 **确实接了 WS2812B，
> 是真实可控的全彩灯**，只是它走单线协议，不能用 `digitalWrite`。板子没有 GPIO2 的
> 用户 LED（GPIO2 只接了自动下载上拉电阻）。

## 2. WS2812B 驱动方案（关键）

**绝不能用 `digitalWrite` / `pinMode(OUTPUT)` 直接驱动** —— WS2812B 是单线严格 800kHz
时序（每个 bit 高低电平宽度 ~0.4µs / 0.8µs），FreeRTOS 多任务 + 中断下 bit-bang 极易
出错、时序不可靠。

**标准做法：Adafruit_NeoPixel（内部走 ESP32 RMT 外设，cycle-accurate，社区标准）**
- 安装：`arduino-cli lib install "Adafruit NeoPixel"`
- 初始化：`Adafruit_NeoPixel s(1, 48, NEO_GRB + NEO_KHZ800);`（1 像素、GPIO48、GRB、800kHz）
- 亮灯：`begin()` → `setBrightness(40)`（暗一点防刺眼）→ `setPixelColor(0, Color(r,g,b))` → `show()`
- 灭灯：`setPixelColor(0, 0,0,0)` → `show()`

**工程化封装（本项目 bsp/led.cpp 范式）**：
- `config.h` 用 `LED_IS_WS2812` 宏切换两套驱动：
  - `=1`：WS2812 路径，含 `LED_PIN=48` / `LED_WS2812_BRIGHTNESS` / `LED_ON_*`(GRB 颜色)
  - `=0`：普通数字 GPIO LED 路径（`LED_ACTIVE_HIGH`）
- `led_set(bool on)`：上层任务/UART 命令只调 `bool` 接口 —— `on` 映射成固定 GRB 颜色，
  `off` 映射成黑色。这样 Task_LED 心跳翻转、按键翻转、UART `led on/off/toggle` **全部
  不用改**，自动作用于 RGB 灯。

## 3. 真机烧录流程

### 3.1 识别 COM 口（关键坑）
Windows 上 `Get-WmiObject Win32_SerialPort` 和 `.NET SerialPort.GetPortNames()` **都返回空**，
漏掉 USB 串口。唯一可靠方式：**pyserial 枚举**。
```python
import serial.tools.list_ports as lp
for p in lp.comports():
    print(p.device, p.description, p.hwid)
```
- 板载 USB-Serial-JTAG：**`USB VID:PID=303A:1001`** → 本项目现为 **COM22**（口会变，
  每次插拔/系统可能变号，务必动态识别，**不要硬编码**）。
- 其余 COM（COM1 主板串口、FTDI 0403:6001 ESP-Prog、CH343 1A86 等）忽略。
- 该口既是**烧录口**（esptool/arduino-cli upload）也是**调试口**（openocd，见
  esp32-cortex-debug skill）。

### 3.2 编译（先确保 elf 最新）
```
arduino-cli compile -j 8 \
  -b esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=default,UploadSpeed=921600 \
  --build-path .build .
```
验收：**0 error / 0 warning**，生成 `.build/<project>.ino.elf`。

### 3.3 烧录
项目自带 `flash-esp32.bat`（纯下载，不编译，需先有 `.build`）：
- **必须用原生 PowerShell 跑 .bat**（Git Bash 跑 .bat 不可信、常无输出误判）：
  ```powershell
  & ".\flash-esp32.bat" COM22 --no-pause 2>&1 | Out-File -FilePath flash_run.log -Encoding utf8
  ```
- 日志：bat 把 esptool upload 输出写到 `flash-esp32.log`，整体 stdout 落到 `flash_run.log`。
- 成功标志：`[FLASH] PASS` + `[RESET] PASS - device restarted.` + esptool `Hash of data verified.`

> 注：flash-esp32.bat 已内置 Python pyserial 枚举串口（无参运行会扫描让你选），
> 也可直接 `flash-esp32.bat COM22` 显式指定。

## 4. 串口验证（功能验收）

烧录用板载口（COM22），串口监控同一口 @ 115200 8N1。用 pyserial 读 N 秒确认运行：
```python
import serial, time
s = serial.Serial("COM22", 115200, timeout=1)
time.sleep(3)  # 等启动横幅
# 读启动横幅 + FreeRTOS 任务列表 + Task_LED 心跳
print(s.read(2000).decode(errors="replace"))
# 发 UART 命令验证 LED 控制链路
for cmd in ("led on\r\n","led off\r\n","led toggle\r\n"):
    s.write(cmd.encode()); time.sleep(2); print(s.read(500).decode(errors="replace"))
```
**验收点**：
- 启动横幅打印芯片/PSRAM/FreeRTOS/任务列表 → 固件正常启动
- UART 回显 `LED ON` / `LED OFF` / `LED TOGGLE` → LED 控制命令链路通
- （肉眼）GPIO48 WS2812B 按预期亮/灭/翻转 → 驱动 + 硬件 ok

## 5. 端到端验收清单

- [ ] 编译：0 error / 0 warning，elf 生成
- [ ] COM 口：pyserial 识别 `303A:1001` 口（动态，勿硬编码）
- [ ] 烧录：`[FLASH] PASS` + `Hash of data verified.` + `[RESET] PASS`
- [ ] 串口：启动横幅 + 任务列表可见，`led on/off/toggle` 回显正常
- [ ] 硬件：GPIO48 WS2812B 实际亮灭符合命令（本项目实测功能 ok ✅）
