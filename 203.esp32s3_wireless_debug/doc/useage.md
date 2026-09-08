# ESP32-S3 CMSIS-DAP 调试探针 — 使用说明 (usage)

> 工程：ESP32-S3 Debug Probe（CMSIS-DAP v1 / HID，SWD 调试）
> 工具链：ESP-IDF v6.1，纯 ESP-IDF + FreeRTOS + TinyUSB + C，无 Arduino
> Phase 1 已完成：编译 + 烧录通过（零错误零警告，tinyusb 回调强链接已验证）

---

## 1. 固件实际引脚分配（已核对 sdkconfig + components/swd/Kconfig）

SWD 引擎采用 GPIO 寄存器级位带时序，所有引脚在 Kconfig 可配置（默认如下）：

| SWD 信号 | ESP32-S3 GPIO | 说明 |
|----------|---------------|------|
| SWDIO    | **GPIO5**     | 双向数据线（探针发请求 / 收响应） |
| SWCLK    | **GPIO4**     | 单向时钟（探针输出） |
| nRESET   | **GPIO6**     | 开漏，拉低=复位目标 |

> 避开已占用引脚：USB OTG D-/D+ = GPIO19/20、UART0 日志(CH340) = GPIO43/44、
> RGB LED = GPIO48、BOOT = GPIO0。SWD 引脚不应与这些冲突。

---

## 2. 接线表（探针 → 外部目标 MCU）

### 2.1 必接（最小可用）

| ESP32-S3 探针侧 | SWD 信号 | 目标板(STM32) 侧 | 说明 |
|-----------------|----------|------------------|------|
| **GPIO5**       | SWDIO    | SWDIO            | 双向数据线；目标内部弱上拉即可 |
| **GPIO4**       | SWCLK    | SWCLK            | 单向时钟（探针输出） |
| **GPIO6**       | nRESET   | nRESET           | 开漏，拉低=复位；目标需上拉（内部或外部 10k） |
| **GND**         | GND      | GND              | **必须共地**，否则不通讯且可能损坏 |

### 2.2 标准连接器对应（目标侧）

**10-pin (1.27mm, Cortex Debug)**：

| 探针 GPIO | 座子引脚 | 信号 |
|-----------|----------|------|
| —         | 1        | VREF / VTref（见 §3 说明） |
| GPIO5     | 2        | SWDIO |
| GPIO4     | 3        | SWCLK |
| —         | 4        | SWO（V1 不接） |
| GPIO6     | 5        | nRESET |
| GND       | 9 / 10   | GND |

**20-pin (2.54mm)**：SWDIO=7、SWCLK=9、nRESET=15、GND=4/10/20、VTref=1。

---

## 3. 不接 / 可选

- **SWO**（10-pin 第4脚 / 20-pin 第13脚）：V1 不抓取 trace，**不接**。
- **VTref**（第1脚）：本固件**未做电压采样**（无 ADC 引脚分配），可悬空；
  若要接，只接目标 **3.3V** 作"电源好"指示，**严禁接 5V**（ESP32-S3 非 5V 耐受）。
- **探针不向外供电**：目标由自身电源供电，不要从探针取电。

---

## 4. 电平与注意事项

- 探针 IO 为 **3.3V**。目标为 3.3V 逻辑可直连；若目标为 **1.8V / 5V**，必须加电平转换。
- SWDIO / SWCLK 通常靠目标内部弱上拉；nRESET 目标内部上拉（或外部 10k）。
- 接线顺序：**先共地，再信号**；上电前确认无短路 / 反接。
- 速率：默认 1MHz、上限 4MHz（Kconfig `DEBUG_SWD_MAX_CLOCK_HZ`），握手成功后再提速。

### 物理定位提示
- 在 ESP32-S3 探针板上找引出 **GPIO4/5/6** 的排针 / 测试点（避开 GPIO0/19/20/43/44/48 等已占用脚）。
  具体焊盘位置对照 `document/esp32s3/ESP32-S3-SCH-V1.4.pdf` 丝印。
- 目标侧：STM32 Nucleo 为 **CN4 (SWD)** 排针，或自定义 10-pin Cortex Debug 座。

---

## 5. 一键编译 / 烧录

提供两套等价脚本：**Git Bash 的 `.sh`** 与 **Windows 原生的 `.bat`**（任选其一）。
两者都靠 `env.*` 提供 IDF 环境、`idf_runner.py` 用 runpy 剥掉 MSYSTEM 再调 idf.py。
**不要**走 `activate.ps1` 或裸 `idf.py`。

### 5.1 Git Bash（.sh）
```bash
bash ./build.sh                 # 增量编译；支持 ./build.sh clean / 参数透传
bash ./flash.sh                 # 无参：扫描并默认 COM21
bash ./flash.sh COM21           # 指定端口
bash ./flash.sh COM21 921600    # 指定端口 + 波特率
```

### 5.2 Windows 原生（.bat，双击或 cmd 运行）
```bat
build.bat                 % 增量编译；build.bat clean 先清再编
flash.bat                 % 无参：扫描系统串口，默认选 COM21（若在列表中）
flash.bat COM21           % 指定端口
flash.bat COM21 921600    % 指定端口 + 波特率
```
- `env.bat` 设置与 `env.sh` 相同的环境变量（PATH 用 `;` 分隔、纯 ASCII 无中文注释）。
- 端口扫描由 `tools/scan_port.py` 完成（IDF 自带 python + pyserial）：列表打印到控制台，
  选定端口作为唯一 stdout 行，供 `flash.bat` 的 `for /f` 捕获。
- `.bat` 内避免使用中文与括号陷阱；若从 Git Bash 继承 `MSYSTEM`，`env.bat` 会清空它。

- `flash.*` 动态扫描 COM 端口并打印列表，端口号会变（如
  COM1 通信端口 / COM21 CH343 下载口 / COM22），**不硬编码**。
- 默认下载口（CH343）= **COM21**（配置默认；实际以扫描到的列表为准）；烧录三段
  （bootloader / partition-table / app）均 `Hash of data verified`，
  最后 `Hard resetting via RTS pin... Done`。

> 完整 clean 重建（约 756 步）可能超过沙箱 7 分钟超时被 SIGTERM 打断在最终 app 链接；
> 增量重建正常完成，被打断后重跑一次 `build` 即可。

---

## 6. 分层架构（速查）

```
CMSIS-DAP 协议层 (cmsis_dap)        DAP 命令解析 / HID 收发
        ↓
Debug Engine (debug_engine)         Cortex-M：halt/run/step/reset/读写寄存器/内存
        ↓
SWD Engine (swd)                    GPIO 位带时序：connect/transfer/reset
```
> USB / Wi-Fi / UART 不得直接操作 SWD；所有访问经 debug_engine 互斥锁串行化
> （为后续 V2 Wi-Fi 预留同一把锁）。

---

## 7. Windows 驱动与主机识别（重要）

本固件是 **CMSIS-DAP v1（HID 类）**。USB 上它就是一块 HID 设备，这是**设计如此**，
不是 bug。主机端必须用系统自带的 **`hidusb.sys`** 驱动，Keil / OpenOCD / pyOCD 才
能通过 HIDAPI 打开它。

### 7.1 驱动黄金法则
- ✅ **正确驱动：USB HID（`hidusb.sys`，系统自动绑定）。** 插上即生效，无需任何额外驱动。
- ❌ **错误驱动：WinUSB / libusbK / WinUSB（Zadig 强装）。** 仅适用于 CMSIS-DAP **v2
  （Bulk/WinUSB 传输）**。给 v1 HID 设备强装 WinUSB 后，Windows 能读出产品名（说明
  描述符正常），但 Keil 等的 HID 通道打不开 → "认得出名字、用不了"。

> 若你已用 Zadig 把该设备改成了 WinUSB，请**还原为系统默认 HID 驱动**（见 7.2），
> 不要保留 WinUSB。

### 7.2 还原默认 HID 驱动（当被 WinUSB 占用时，Windows 10）

> 关键认知：CMSIS-DAP **v1 在 USB 上就是 HID 设备**，出现在"人体学输入设备"下是**正常且正确**的，
> 不是"被误识别"。之前看到的"USB HID 设备"其实是对的；换成 WinUSB 后才真正坏了。

1. **设备管理器**（`Win+X` → 设备管理器）→ 顶部菜单"查看" → **"按连接列出设备"**，方便定位。
2. WinUSB 下该设备通常在 **"通用串行总线设备"** 分支，名为
   "ESP32-S3 CMSIS-DAP Debug Probe" 或 "WinUSB Device"；也可能在"人体学输入设备"下但驱动为 WinUSB。
3. 右键该设备 → **卸载设备** → **务必勾选"尝试删除此设备的驱动程序"** → 确定。
   （这一步是关键：删掉 Zadig/WinUSB 留下的第三方驱动包，否则 Windows 10 重插后会再次自动绑回 WinUSB。）
4. **拔下原生 USB 线 → 等 5 秒 → 重新插入**。Windows 10 因它是 HID 类设备而**自动安装系统自带的 `hidusb.sys`**。
5. 验证：设备应回到 **"人体学输入设备（Human Interface Devices）"** 分支下，
   名称形如 "ESP32-S3 CMSIS-DAP Debug Probe" / "HID 兼容的供应商定义设备"，**无黄色叹号**。
   （注意：只要驱动是 `hidusb.sys`、不再是 WinUSB，就算成功，具体显示名不重要。）

> Windows 10 提示：若重插后仍走 WinUSB，多半是步骤 3 没勾"删除驱动程序"。重复 2–4 并确保勾选。
> 也可改用 **Zadig**：选目标设备 → 右侧下拉选 **"HID-compliant device"**（即 hidusb.sys）→ Install/Replace。

### 7.3 Keil 中识别 / 使用
1. 驱动为 `hidusb.sys` 后，Keil μVision → `Options for Target` → `Debug` →
   选 **CMSIS-DAP**（或 "CMSIS-DAP Debugger"）。
2. 点 `Settings`：在 "CMSIS-DAP" 列表里应能看到本探针（含 eFuse 序列号）；
   `Port` 选 **SWD**，下方应扫描出目标 MCU 的 DP/AP（需目标已上电且 SWD 三线接好）。
3. 若列表为空：确认 7.2 驱动已还原、原生 USB（GPIO19/20）已插、且设备管理器无黄色叹号。

### 7.4 两个 USB 口别混淆
- **原生 USB（GPIO19/20）** = CMSIS-DAP HID 调试通道 = Keil 用的口（本节主角）。
- **CH340（GPIO43/44, 即 COM21）** = 仅 UART 日志 / 烧录串口，**不是**调试接口。
  给 COM21 装驱动不影响调试；给原生 USB 装错驱动（WinUSB）才会让 Keil 失效。

### 7.5 关于"设备被识别成 USB HID"
CMSIS-DAP v1 在设备管理器里**本来**就位于"人体学输入设备"下、表现为 HID 设备，
只要产品名含 "CMSIS-DAP" 且驱动是 `hidusb.sys`，调试软件即可正常识别。
这不是误识别，无需为它换驱动。

### 7.6 还原成 hidusb.sys 后 Win10 上 Keil 仍列表为空
按优先级排查（驱动正确是前提，上面已确认）：
1. **确认驱动真的回来了**：设备管理器里该设备 → 右键"属性" → "驱动程序" →
   "驱动程序详细信息"，应看到 `hidusb.sys`，**不是** `winusb.sys` / `libusbK.sys`。
2. **用 OpenOCD / pyOCD 交叉验证**（比 Keil 更宽松，能排除 Keil 配置问题）：
   - `pyocd list` 或 `pyocd cmd -u <probe serial> -t <target>` 应能看到本探针；
   - OpenOCD：`openocd -f interface/cmsis-dap.cfg -f target/<mcu>.cfg`。
   若它们能认出而 Keil 不能 → 是 Keil 侧的 Debug 驱动/工程配置问题，非固件。
3. **确认插的是原生 USB（GPIO19/20）那根线**，不是 CH340（COM21，那是烧录/日志口，不参与调试）。
4. **关闭 USB 选择性暂停**：设备管理器 → 系统设备 → "USB 根集线器" → 电源管理 →
   取消"允许计算机关闭此设备以节约电源"。
5. 若仍不行，提供：设备管理器该设备的"硬件 ID" + "驱动程序详细信息"截图，我再定位。

### 7.7 确认你配置的是"正确的设备"（看硬件 ID，别认错！）
设备管理器里 HID 设备很多（键盘、鼠标、接收器…），极易点错。**我们的探针硬件 ID 固定为：**
- USB 设备节点：`USB\VID_303A&PID_8502`（VID 0x303A = Espressif，PID 0x8502）
- HID 节点：`HID\VID_303A&PID_8502...`（Keil/OpenOCD 实际打开的就是它）

> 反例（**不是**本探针，别动）：`VID_413C`（Dell）、`VID_046D`（Logitech）、`VID_0D8C`（音频）等。
> 例：用户曾误把 `HID\VID_413C&PID_2113`（Dell 复合外设，驱动 hidserv.inf 本就正常）当探针去换驱动，
> 结果真探针（VID_303A）没动到，Keil 自然看不到。

定位探针的可靠方法：
1. 设备管理器 → 查看 → "按连接列出设备"。
2. **拔掉探针原生 USB（GPIO19/20）→ 看哪个设备消失 → 插上 → 看哪个出现**，即探针。
3. 右键 → 属性 → 详细信息 → 属性选 **"硬件 ID"**，核对是否含 `VID_303A&PID_8502`。
4. 只对 VID_303A 设备做驱动处理；其他 HID 设备一律不管。

### 7.8 "正确驱动"长什么样（对照用）
探针在"人体学输入设备"下、硬件 ID `HID\VID_303A&PID_8502...` 且属性如下，即**正确，勿再改动**：
- 驱动程序：`input.inf` / 部分 `HID_Raw_Inst.NT`（底层即 `hidusb.sys`，Microsoft 系统自带）
- 类 GUID：`{745a17a0-74d3-11d0-b6fe-00a0c90f57da}`（HID 类）
- 提供商：Microsoft；日期 2006 / 版本 10.0.19041.x（Windows 内置）
> 反例（错误）：驱动为 `winusb.sys` / `libusbK.sys` / `WinUSB Device` → 需按 7.2 还原。
> 注意：Dell 等设备（如 `VID_413C&PID_2113`，驱动 `hidserv.inf`）是无关外设，驱动正常也别动。

### 7.9 驱动正确但 Keil 仍看不到 → 下一层排查
驱动已正确（hidusb.sys）后，问题转为：固件是否响应 / Keil 是否识别自定义 VID/PID。
1. **交叉验证（首选）**：`pyocd list` 或 `openocd -f interface/cmsis-dap.cfg -f target/<mcu>.cfg`。
   能认出 → 固件+USB 正常，问题在 Keil（多半是 VID/PID 白名单）；也认不出 → 固件未响应，查固件侧。
2. **Keil VID/PID 白名单**：Keil CMSIS-DAP 部分版本只认已知 ID（NXP 0x0D28、ST 0x0483 等），
   本探针 `0x303A/0x8502` 为自定义 ID，可能不被列出。pyOCD/OpenOCD 为通用 HID 扫描通常能认。
3. 必要时将固件 VID/PID 改回 Keil 已知 CMSIS-DAP 标准 ID（如 0x0D28/0x0204），或加串口调试日志
   确认固件确实收到并处理了 DAP 命令。

### 7.10 固件侧已修复：OpenOCD `CMD_INFO failed`（2026-09-08）
曾报 `Error: CMSIS-DAP command CMD_INFO failed.`，根因在**固件协议层**（非驱动）：
- **`cmsis_dap.c` 的 `ID_DAP_Info` 响应用"数据长度"填了 `response[1]`，但 CMSIS-DAP 规范要求
  `response[1]` 回显 InfoType**，数据从 `response[2]` 起。OpenOCD 校验 `buf[1] != info_id` 失败。
  已改为 `response[1] = info_type`（回显），数据落在 `response[2..]`。
- `dap_info` 的 InfoType case 由 1/2/3/4 改为 ARM 标准 0x00/0x01/0x02/0x03（Vendor/Product/Serial/FW）。
- `dap_task` 原把 `cmsis_dap_execute` 的编码返回值（高16=请求字节数，低16=响应长度）整段当 `len`
  传入 `usb_device_send_response`（靠 clamp 到 64 才没崩）；已改为只取低 16 位作为真实响应长度。
- 响应缓冲 `memset` 清零（保证 Info 字符串隐式 null 终止）。

> CMSIS-DAP v1 HID 响应格式：`[cmd(0x00), InfoType(回显), data...]`；字符串类型**不带显式长度**，
> 靠零填充隐式 null 终止；数值类型（0xF0 能力位 / 0xFF 包大小 / 0xFE 包计数）紧随 byte2。
> 修复后 `build` 零警告、`flash -p COM21` 已烧录验证。重跑 OpenOCD 应不再报 CMD_INFO failed。


