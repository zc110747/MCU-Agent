# ESP32-S3 远程硬件调试网关 — 操作与硬件手册

> 本手册面向**使用者 / 接线 / 运维**：硬件接线、烧录、连接、Web/MQTT/WebSocket 操作、
> 自测脚本。开发过程见 `README.md`，完整协议见 `DELIVERY.md`。

---

## 1. 硬件准备

| 物品 | 说明 |
|------|------|
| ESP32-S3 模组/开发板（N16R8：Flash 16MB / PSRAM 16MB） | 调试网关本体 |
| USB 数据线 | 供电 + 烧录 + CDC 串口日志 |
| 目标 MCU 板 | 被监视的目标（UART 日志源） |
| ADS1115 模块（可选） | 4 通道外部 ADC，经 I2C 接入 |
| 杜邦线若干 | 接线 |

---

## 2. 引脚分配表

| GPIO | 角色 | 方向 | 说明 |
|------|------|------|------|
| 19 / 20 | USB D- / D+ | 保留 | **禁止占用**（USB 串口/烧录口） |
| 48 | RUN LED (WS2812B) | 输出 | 板载运行灯，由 **LED 控制器独占**（Web Interface 页可控制，见 §5.3.2） |
| 17 | UART_RX | 输入 | ← 目标 MCU **TX** |
| 18 | UART_TX | 输出 | → 目标 MCU **RX** |
| 8  | I2C SDA | 开漏 | ADS1115 SDA |
| 9  | I2C SCL | 输出 | ADS1115 SCL |
| 4 / 5 / 6 / 7 | GPIO 监视 | 可配 | 默认输入上拉，可切输出 |
| 任意空闲 GPIO（避开 19/20/48、GPIO4..7、17/18、8/9、0/45/46） | PWM 输出 | 输出 | 用户在 Interface 页选一路，LEDC 驱动（见 §5.3.3） |

> PinManager 在启动时强制 `claim()`：任何模块未成功占用即整体失败，**19/20 绝对不可占用**（USB D-/D+）；
> GPIO48 由 LED 控制器独占（用户不要在外部接信号）；PWM 占用用户所选脚，也不会与已有模块重复占用。

---

## 3. 接线图

### 3.1 UART（接目标 MCU）
```text
目标 MCU            ESP32-S3
TX      ────────►  GPIO17 (UART_RX)
RX      ◄────────  GPIO18 (UART_TX)
GND     ────────►  GND
```
- 默认 115200/8N1；支持 9600…921600、5–8 数据位、1–2 停止位、无/奇/偶校验（Web/MQTT 改）。
- 支持文本 / HEX 两种显示模式。

### 3.2 ADC（ADS1115，可选）
```text
ADS1115            ESP32-S3
VCC     ────────►  3.3V
GND     ────────►  GND
SDA     ────────►  GPIO8
SCL     ────────►  GPIO9
ADDR    ────────►  GND   (I2C 地址 0x48)
A0..A3  ◄────────  被测电压（超量程需外部分压）
```
- 4 通道（CH0..3），默认量程 ±6.144V（gain 2/3）。
- 实际电压 = `采样值 × lsb × 分压比 + 偏移`；分压比/偏移可在 Web 配置。
- 默认采样率 10 Hz（100 ms），可选 1/5/10/20/50/100 Hz。

### 3.3 GPIO 监视（可选）
GPIO4/5/6/7 接被测数字信号；默认输入上拉，变化即上报；可经 Web/MQTT `gpio_set` 提升为输出。

---

## 4. 烧录（首次上电前）

确保已装 `arduino-cli`（PATH 或本地），外部库由脚本首次自动安装。

```bat
:: 一键编译
build_oneclick.bat

:: 一键烧录（双击即可；自动扫描 ESP32 端口）
flash-esp32.bat
flash-esp32.bat COM7          :: 指定端口
flash-esp32.bat COM7 monitor  :: 烧录后开串口监视（115200）
flash-esp32.bat --no-pause    :: 非交互（CI）
```
- 烧录后脚本额外给一次 DTR/RTS 复位脉冲，确保进入用户程序。
- **编译缓存（自动）**：arduino-cli 1.5.x 已废弃 `--build-cache-path`，改为**自动缓存**已编译的
  core/库（存于其 data 目录），只要保留 `--build-path .build` 不删除，第二次及之后编译明显更快；
  `.build/` 已加入 `.gitignore`。强制全量重编：`rm -rf .build` 或用 `arduino-cli compile --clean`。
- 板子插在 `COMxx`（VID `303A:1001`，即 ESP32-S3 内置 USB-Serial-JTAG）。
- 也可用 VS Code：`Ctrl+Shift+B` → `Build (arduino-cli)`；调试用 `.vscode/launch.json` 的
  Cortex-Debug 配置（elf 指向 `.build/202.esp32s3_hw_detect.ino.elf`）。

---

## 5. 连接（AP 模式 / STA 模式）

### 5.1 软件接入（PC ↔ 设备）
首次上电（NVS 未存 WiFi 账号）设备**默认进入 AP 热点模式**：

| 项 | 值 |
|---|---|
| 热点名 | `ESP32S3-Debugger-XXXX`（XXXX = MAC 末 4 位十六进制大写） |
| 热点密码 | `debugger123` |
| 设备 IP | `192.168.4.1`（Web:80 / WebSocket:81） |
| 运行指示 | GPIO48 RUN LED 闪烁 = 正常 |
| Web 鉴权 | Basic，用户名 `admin` / 密码 `admin`（默认，可在 Config 页改） |

**方式一 · 连设备自带热点（最快）**：USB 供电 → 等 3–5 s 启动 → PC 连热点 →
浏览器开 `http://192.168.4.1`（仪表盘自动连 `ws://192.168.4.1:81` 实时推送）。
串口助手开烧录口 @115200 可见启动日志与 AP 名。

**方式二 · 接入局域网（STA）**：在仪表盘 WiFi 配置区填路由器 SSID/密码 → Save →
设备重启切 STA。成功后**自带热点消失**，从串口日志或路由器 DHCP 列表取 STA IP，
浏览器开 `<STA_IP>:80`。
⚠️ 若 STA 连不上会**自动回退 AP**；"找不到热点"说明它已连上你的网，去查 STA IP。

### 5.2 Web 仪表盘
1. 打开 `http://<设备IP>/`，顶部显示连接状态，自动连 `ws://<host>:81/`。
2. 功能页：Hardware（硬件信息）/ UART / Interface（GPIO 监视 + WS2812 + PWM + ADC）/ Config / OTA / Logs。
   - **Interface 页为"零轮询"设计**：连上 WebSocket 后，设备每 `400 ms` 主动推送一次 `state` 快照，
     页面所有读数（GPIO 电平/方向、WS2812 实时输出、PWM 实际参数、ADC 电压）都由该快照驱动刷新，
     浏览器不再定时拉 HTTP。断线后 3 s 自动重连，重连成功立即补一帧快照。
   - 原 **GPIO** 页已更名为 **Interface**，并合并了原本独立的 ADC 页（ADC 电压读数直接在 Interface 页实时显示）。
   - 原 **Dashboard** 与 **System** 页合并为 **Hardware** 页：集中展示芯片型号、时钟频率、当前启用硬件、Web 访问 IP+端口、MQTT 地址+端口（详见 §5.3）。
3. 配置类操作需填 **Web 密码**（默认 `admin`，可在 Config 页修改）。
4. OTA：OTA 页选 `.bin` 上传（需密码），成功后自动重启；NVS/WiFi/MQTT/设备ID 保留。

---

## 5.3 Interface 页操作（GPIO / WS2812 / PWM / ADC）

Interface 页（原 GPIO 页）集中了所有板级数字/模拟外设的实时控制与读数；原独立 ADC 页已合并至此。
页面分为 4 张独立卡片：**GPIO Monitor / WS2812 Status LED / PWM Output / ADC Read**。

> **状态驱动模型**：点击按钮只负责下发命令（WS 入站 JSON），界面上的**显示值一律来自设备推送的
> `state` 快照**（400 ms 一帧）+ 事件消息，绝不用"下发即回显"假装成功。因此页面上看到的永远是
> 硬件的真实状态：GPIO 是 `digitalRead()` 回读的电平，PWM 是 LEDC 量化后的实际参数，WS2812 是当前
> 真实点亮的 RGB，ADC 是最近一次采样值。

### 5.3.1 GPIO 监视（GPIO4..7）
默认输入上拉；点 **SET / CLEAR** 经 `gpio_set` 提升为输出并驱动，方向掩码写入 NVS 重启保持。

- 每行显示三列：**引脚 / 方向（IN / OUT）/ 电平（HIGH / LOW）**。
- 点击 SET/CLEAR 后，设备先 `digitalWrite()` 再 **`digitalRead()` 回读真实电平**，把回读值写入
  `GPIO_STATE` 事件与 `state` 快照。若引脚被外部拉死（如短路到 GND），界面会如实显示 LOW 而非
  假装在 HIGH。
- 处于输入方向的脚，由 50 ms 轮询任务检测外部电平变化，仅在**发生变化**时推送事件。

### 5.3.2 WS2812 状态灯（GPIO48）
板载 WS2812B 运行灯，支持 5 种状态：**关闭 / R 闪烁 / G 闪烁 / B 闪烁 / RGB 循环**。

- 点对应按钮即下发 `ws2812_set`，选中态用**中性描边**（无彩色高亮，保持全站单色风格）。
- 按钮下方 `ledCur` 一行显示**模式 + 实时输出**，例如：
  - `OFF`（关闭，未点亮）
  - `GREEN BLINK | ON rgb(0,255,0)`（绿色闪烁的"亮"半周期）
  - `GREEN BLINK | OFF`（同一模式的"灭"半周期）
  - `RGB CYCLE | ON rgb(0,0,255)`（循环到蓝色段）
  即除了模式，还能直接看到**此刻灯珠实际输出的颜色**，闪烁/循环过程肉眼可见地在界面上跳变。
- 亮度由 `LED_BRIGHTNESS=40` 固定（防刺眼），快照中一并给出 `brightness` 字段。

### 5.3.3 PWM 输出
在 Interface 页选择一路**空闲 GPIO**（下拉框列出可用脚，已自动排除 USB/串口/I2C/GPIO 监视/LED 等占用脚），
设置 **周期（µs，如 1000 = 1kHz）** 与 **占空比（0–100%）**，点 **Apply** 启动；点 **Stop** 关闭并释放引脚。
驱动方式为 ESP32 内置 LEDC（周期 ≥ 20 µs 时 12-bit，高频自动降到 8-bit，见快照 `resolution` 字段）。
引脚经 PinManager 占用，冲突会被拒绝（页面提示后保持原状）。

- Apply 成功后，卡片顶部显示**设备实际生效的参数**，例如 `GPIO21 1000us 25% (1.00 kHz)`。
- `period` / `duty` / `pin` 输入框会被设备回传值回填（**正在编辑的输入框不覆盖**，免打断输入），
  因此若输入的参数被 LEDC 量化修正，界面显示的是修正后的真实值。
- Stop 后显示 `PWM idle`，引脚 detach 并释放回 PinManager。

### 5.3.4 ADC 读数
原独立 ADC 页已合并到此处：4 通道电压实时刷新（10 Hz），并显示原始值。
无 ADS1115 时自动回退内部 ADC（GPIO1/2）。量程 / 分压比在 Config 页 ADC 区配置。

- 表格固定 4 行（CH0..3），每帧 `state.adc[]` 刷新电压（V）与原始码值（raw）。
- 曲线图按**峰值自适应缩放**（`scale = peak × 1.15`），左上角标注 `peak x.xx V`，无需手动选量程。
- 标题后标注当前采样源，如 `[ads1115]` 或 `[internal-adc1]`。

---

## 6. MQTT 接入（可选）

默认 broker `192.168.10.1:1883`，可在 MQTT 配置区改。
Topic 前缀 `remote-debugger/<device_id>/`，`device_id = esp32s3-XXXXXXXX`（板载 MAC）。

| 方向 | Topic | 内容 |
|------|-------|------|
| 上行 | `.../status` | 状态/遗嘱（LWT `offline`，连上后由 SYSTEM 推送） |
| 上行 | `.../uart/rx` | UART 接收 |
| 上行 | `.../uart/tx` | UART 发送回显 |
| 上行 | `.../adc/ch0..3` | ADC 各通道电压 |
| 上行 | `.../gpio` | GPIO 状态变化 |
| 上行 | `.../system` | 系统状态 JSON |
| 上行 | `.../event` | ERROR/事件 JSON |
| 下行 | `.../cmd` | 上位机 / AI 下发命令 |

### 6.1 下行命令（`.../cmd` 或 WebSocket 入站）
```json
{ "cmd":"uart_config", "baud":115200 }
{ "cmd":"uart_tx", "data":"AT\r\n" }
{ "cmd":"gpio_set", "gpio":4, "value":1 }
{ "cmd":"ws2812_set", "mode":"off" }            // off | r | g | b | cycle
{ "cmd":"pwm_set", "pin":21, "period":1000, "duty":50 }   // 启动 PWM（周期 µs，占空比 %）
{ "cmd":"pwm_set", "active":false }             // 停止 PWM 并释放引脚
{ "cmd":"adc_read" }
{ "cmd":"wifi_config", "ssid":"...", "pass":"..." }
{ "cmd":"mqtt_config", "broker":"1.2.3.4","port":1883,"user":"","pass":"","keep":30,"tls":false }
{ "cmd":"device_reset" }
```
所有命令经 `handleJsonCommand` 统一校验（参数/GPIO 合法性、越权拒绝），结果以日志/事件反馈。

---

## 7. WebSocket 协议（端口 81）

入站（浏览器 / AI → 设备）：
```json
{ "cmd":"uart_tx", "data":"AT+RST" }
{ "cmd":"gpio_set", "gpio":4, "value":1 }
```
出站（设备 → 浏览器）：
```json
{ "type":"uart",    "timestamp":..., "encoding":"text", "data":"Booting..." }
{ "type":"uart_tx", "timestamp":..., "encoding":"text", "data":"AT+RST" }
{ "type":"adc",     "channel":0, "raw":1234, "voltage":3.301, "timestamp":... }
{ "type":"gpio",    "gpio":4, "state":1, "timestamp":... }
{ "type":"led",     "gpio":48, "mode":4, "mode_str":"cycle", "on":1, "r":0, "g":255, "b":0,
  "brightness":40, "timestamp":... }        // mode: 0=off 1=r 2=g 3=b 4=cycle
{ "type":"pwm",     "active":1, "pin":21, "period":1000, "duty":25, "freq":1000,
  "resolution":12, "timestamp":... }       // freq/resolution 为 LEDC 实际生效值
{ "type":"log",     "line":"[123][INFO][UART] ..." }
{ "type":"system",  "device":..., "uptime":..., "free_heap":..., ... }
{ "type":"state",   "ts":..., "gpio":[...], "led":{...}, "pwm":{...}, "adc":[...], "adc_src":"..." }
```
> 禁止浏览器高频 HTTP polling 拉 UART；全部走 WebSocket 实时推送。

### 7.1 `state` 状态快照（服务器主动推送，400 ms/帧）

设备**仅在有浏览器连接时**（`_clients > 0`）每 `STATE_PUSH_INTERVAL_MS = 400` ms 由 ws_task 直接
构造并广播完整 Interface 状态；无客户端时零开销。该帧**不进 EventBus 队列**（避免把 `DebugEvent`
从 ~220 B 撑到 800 B+），直接读各模块缓存 + `broadcastTXT()`。

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

| 字段 | 含义 |
|------|------|
| `gpio[].state` | `digitalRead()` 的**真实回读电平**（0/1），非下发值 |
| `gpio[].dir`   | 1 = 输出，0 = 输入（上拉） |
| `led.on/r/g/b` | 灯珠**此刻**实际输出的颜色；`on=0` 表示处于闪烁的灭半周期或已关闭 |
| `pwm.freq`     | LEDC 换算后的实际频率（Hz），可能与按 `period` 算出的理想值有量化差异 |
| `pwm.resolution` | 实际采用的分辨率（8..12 bit，按 `80MHz / 2^res >= freq` 自动降档） |
| `adc[].voltage` | 已代入量程 / 分压比 / 偏移换算后的电压（V） |
| `adc_src`      | `ads1115` 或 `internal-adc1`（未接外部 ADC 时自动回退） |

浏览器收到 `state` 后调用 `applyState()` 分发到 4 个渲染函数，覆盖 GPIO / WS2812 / PWM / ADC 四张卡片。
新客户端连接时服务端立即**清空调帧计时**（`_lastState = 0`），保证首帧不用等到下一个 400 ms。

---

## 8. 真机验收（tools/verify）

纯 stdlib、英文输出、pass/fail 计数。设备 IP 用参数或环境变量 `RHD_IP` 指定（默认 AP `192.168.4.1`）。

| 脚本 | 验证点 |
|---|---|
| `verify_web.py` | 根路径(Hardware 页) 200、`/api/status` JSON 字段、无凭证 401 鉴权、`/api/wifi`、`/api/logs` |
| `verify_gpio.py` | 4 路监视脚 [4,5,6,7]、GPIO4 写 1/写 0 回读、非监视脚 13 与保留脚 48 被 400 拒绝 |
| `verify_adc.py` | `/api/adc/read` 4 通道 raw+voltage、`/api/adc/config` fsr；ADS1115 未接时 WARN 不 FAIL |
| `verify_ws.py` | WS `:81` 握手 101、持续接收带 type 的 JSON、**`state` 快照可观测且含 gpio/led/pwm/adc 四段**、心跳观测 |
| `verify_interface.py` | **Interface 四项实时性端到端**：WS 下发命令 → 回读 `state` 断言（GPIO SET 后 `state=1` 且 `dir=1`、CLEAR 后 `state=0`；`ws2812_set cycle` 后 2 s 内 `(on,r,g,b)` 出现 ≥2 种组合；`pwm_set` 参数被镜像且 stop 后 `active=0`；`adc[]` 4 通道 voltage 为数值）；结束恢复原 LED 模式 |
| `run_all.py` | 汇总以上 5 项，输出 `TOTAL: n/5 scripts passed`，退出码 0/1 |

```bat
cd tools\verify
python run_all.py                :: AP 模式（连上设备热点后跑）
python run_all.py 192.168.1.50   :: STA 模式（设备已入局域网）
```

---

## 9. 注意事项 / FAQ

- **保留脚**：19/20 绝对不可接调试信号（USB D-/D+）。GPIO48 是 WS2812B 状态灯，由专用控制器独占，也不应接外部信号。
- **WS2812 亮度**：固件内 `LED_BRIGHTNESS=40`，偏暗防刺眼；如需更亮改 `bsp/ws2812_led.cpp` 后重编。
- **ADS1115 未接**：`verify_adc.py` 会 WARN（raw 全 0），属预期；接好 I2C（SDA=8/SCL=9/ADDR→GND）+ 信号源即可看到真实电压。未接时固件自动回退到内部 ADC（GPIO1/2）。
- **Web 密码**：默认 `admin`；改后请记牢，配置文件/OTA 均需它（NVS 持久化）。
- **GPIO 方向持久化**：经 `gpio_set` 提升为输出的脚，方向掩码写入 NVS，重启后保持输出。
- **界面读数不刷新**：先确认顶部连接状态为已连接。Interface 页的所有读数都来自 WS `state` 快照，
  WebSocket 断开（或浏览器屏蔽了 `ws://`）时读数会停在最后一帧；页面每 3 s 自动重连，无需手动刷新。
- **点了 SET 仍显示 LOW**：这是**预期行为**，说明外部电路把该脚拉低（短路/强下拉/驱动能力不足）。
  界面显示的是 `digitalRead()` 的真实回读值，不是下发值——先查硬件。
- **PWM 频率与设置值有偏差**：LEDC 以 `80 MHz / 2^resolution` 为时钟源，周期较短时会自动降低分辨率
  （12 → 8 bit）；界面显示的是量化后的真实频率（见 `state` 的 `pwm.freq` / `pwm.resolution`）。
