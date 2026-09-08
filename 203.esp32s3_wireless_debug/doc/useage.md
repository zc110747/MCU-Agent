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

### 1.1 JTAG 引脚分配（新增，TCK/TMS 与 SWD 复用同一物理引脚）

JTAG 模式为**纯 JTAG**，不做 SWD↔JTAG 切换。TCK/TMS 直接复用 SWD 的 SWCLK/SWDIO，
只需再引出 TDI/TDO/nTRST 三个引脚（默认 GPIO7/8/9，Kconfig `DEBUG_TDI_GPIO` /
`DEBUG_TDO_GPIO` / `DEBUG_NTRST_GPIO` 可改）：

| JTAG 信号 | ESP32-S3 GPIO | 复用说明 |
|-----------|---------------|----------|
| TCK       | **GPIO4**     | 复用 SWCLK（探针输出时钟） |
| TMS       | **GPIO5**     | 复用 SWDIO（探针 TAP 状态机驱动） |
| TDI       | **GPIO7**     | 新增，探针→目标，空闲高（bypass=1） |
| TDO       | **GPIO8**     | 新增，目标→探针，只读输入 |
| nTRST     | **GPIO9**     | 新增，开漏，拉低=复位 JTAG 链（未接可设 -1） |
| nRESET    | **GPIO6**     | 复用 SWD 的 nRESET（系统复位，非 JTAG 链复位） |

> ⚠️ **同一时刻仅启用一种协议**：`DAP_Connect` 选 SWD(1) 或 JTAG(2)。两种模式共享 TCK/TMS 引脚，
> 但 TDI/TDO/nTRST 仅在 JTAG 模式被驱动；SWD 模式下 nRESET 才有效（与 §2 一致）。
> 飞线（flying-wire）连接时，先用 **100kHz** 握手（`DAP_SWJ_Clock` 下发），成功后再提速。

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
CMSIS-DAP 协议层 (cmsis_dap)        DAP 命令解析 / HID 收发 / 双端口(SWD+JTAG)分发
        ↓
Debug Engine (debug_engine)         Cortex-M：halt/run/step/reset/读写寄存器/内存
        ↓                     ↘
SWD Engine (swd)               JTAG Engine (jtag)
GPIO 位带时序：                 GPIO 位带时序（TCK/TMS 复用 SWD）：
connect/transfer/reset          ir/transfer/read_idcode/sequence/reset
```
> USB / Wi-Fi / UART 不得直接操作 SWD/JTAG；所有访问经 debug_engine 互斥锁串行化
> （为后续 V2 Wi-Fi 预留同一把锁）。JTAG 引擎是 SWD 引擎的**对等层**，共用 TCK/TMS 引脚，
> 由 `DAP_Connect` 选端口决定走哪条链路。

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

### 7.10 固件侧已修复：OpenOCD `CMD_INFO failed`（2026-09-08，最终正确版）
现象：`Error: CMSIS-DAP command CMD_INFO failed.`（驱动 `hidusb.sys` 已正确的前提下）。
**根因在固件协议层**，已对照 ARM 官方 `DAP.c` / `DAP.h`（参考工程 `007.stm32h743_cmsis_dap`）修正：

- **InfoType 键必须为 1-based**：ARM `DAP.h` 定义
  `DAP_ID_VENDOR=1 / PRODUCT=2 / SER_NUM=3 / FW_VER=4 / CAPABILITIES=0xF0 / PACKET_COUNT=0xFE / PACKET_SIZE=0xFF`。
  OpenOCD/Keil 发送的正是这些值。原 `dap_info` 用了 0x00/0x01/0x02/0x03（0-based），
  导致所有字符串查询 `dap_info()` 落到 `default` 返回长度 0，OpenOCD 收到空响应判失败。
  → 已改回 `case 1/2/3/4`（对齐 ARM `DAP.h`）。
- **`ID_DAP_Info` 响应 `response[1]` 必须是"数据长度"，不是 InfoType 回显**：
  ARM `DAP.c` 的 `DAP_Info` 返回 `num`（长度），调用处写 `*response = (uint8_t)num;`，
  即 `response[1] = length`，`response[2..] = data`。之前误改成回显 InfoType 反而错误。
  → 已改回 `response[1] = (uint8_t)num`（长度），`response[0]=cmd`，`response[2..]=data`。
- `dap_task` 取响应长度已从"编码返回值整段"改为只取低 16 位（响应字节数）；响应缓冲 `memset` 清零。
- 参考工程 `007` 直接用 ARM 官方 `DAP.c`（`third_party/CMSIS-DAP/DAP.c`）接入
  `DAP_ExecuteCommand`，零改动即通过 OpenOCD——本工程因走 ESP32 SWD 驱动，采用"协议层对齐 ARM、
  底层 SWD 用自有 `swd.c`"的等价方案。

> 修复后 `build` 零警告（仅 ccache 提示，无害）、`flash -p COM21` 三段 `Hash of data verified` 烧录通过。

### 7.11 COM21 调试日志 + OpenOCD 功能测试（回调可见性）
ESP_LOG 走 UART0（CH340 = **COM21**），固件已在关键路径加打印，配合 OpenOCD 即可在串口看到完整命令流：
- **HID 回调边界**（`components/usb_device/usb_device.c` `tud_hid_set_report_cb`）：
  `HID OUT cmd=0xXX len=N` —— 每个到达的原始 CMSIS-DAP 请求。
- **命令分发**（`components/cmsis_dap/cmsis_dap.c`）：
  `>> req cmd=0xXX len=N`（顶层）、`DAP cmd id=0xXX`（每个子命令）、`DAP_Info id=N len=N`。
- 上电时 `app_main` 还打印 SWD 引脚 / USB VID-PID / 产品名，便于确认固件已起来。

**功能测试步骤**
1. 用 `flash.bat`（或 `bash ./build.sh && flash.bat`）烧录最新固件到 COM21。
2. 打开 COM21 串口监视（如 `idf.py monitor -p COM21` 或任意串口工具，115200 8N1），观察上电日志。
3. 接好目标 MCU 的 SWD 三线（SWDIO=GPIO5 / SWCLK=GPIO4 / nRESET=GPIO6 / GND 共地，见 §2）。
4. 另开终端跑：`openocd -f interface/cmsis-dap.cfg -f target/stm32h7x.cfg`
   - 预期：**不再**报 `CMD_INFO failed`；COM21 串口可见连续的 `HID OUT`/`DAP cmd` 日志；
     OpenOCD 依次完成 Info → Connect(SWD) → SWJ_Pins/Clock → SWD_Configure → Transfer(读 DPIDR) → 连上目标。
   - 若目标未接，Transfer 会返回 WAIT/NO_RESPONSE（正常），OpenOCD 报 "no device found"，属预期，非固件 bug。
5. 后续可在 Keil 选 CMSIS-DAP（注意 §7.9 的 VID/PID 白名单提示）。

---

## 8. JTAG 模式（2026-09-08 新增，纯 JTAG，不切换）

JTAG 引擎（`components/jtag/jtag.c`）是 ARM 官方 `JTAG_DP.c` 的逐行移植（寄存器级 GPIO
位带时序，与 SWD 同一套延迟模型），保证线协议与 OpenOCD/Keil 期望**逐位一致**。
`CMSIS-DAP v1` 命令 `0x14/0x15/0x16/0x17/0x18/0x19`（JTAG_Sequence/Configure/IDCODE/
Transfer/TransferBlock/WriteAbort）已完整实现并接入 `cmsis_dAP.c` 分发。

> 注意：标准 OpenOCD/pyOCD 用 **0x17=JTAG_Transfer、0x18=JTAG_TransferBlock**；
> 参考工程 `007` 把这两个命令号改成了 SWO 命令号（0x17/0x18 在 007 里是 SWO_Transport/SWO_Mode），
> 那是为了 007 自定义固件；**本工程保持标准命令号**，以确保通用 OpenOCD 兼容。

### 8.1 接线表（探针 → STM32 目标 JTAG）

| 探针 GPIO | JTAG 信号 | STM32 目标      | JTAG 标准脚 |
|-----------|-----------|-----------------|-------------|
| GPIO4     | TCK       | PA14            | TCK         |
| GPIO5     | TMS       | PA13            | TMS         |
| GPIO7     | TDI       | PA15            | TDI         |
| GPIO8     | TDO       | PB3             | TDO         |
| GPIO9     | nTRST     | PB4（可选）      | nTRST       |
| GPIO6     | nRESET    | NRST            | nRESET      |
| GND       | GND       | GND             | —           |

> 飞线 / 长线连接请先 100kHz 握手（见 §8.2），成功后再提速到 1MHz。
> nTRST 若目标未引出，可把 `DEBUG_NTRST_GPIO` 设成 -1（固件不驱动该脚）。

### 8.2 OpenOCD 功能测试（JTAG）

1. 烧录最新固件（`flash.bat` / `bash ./build.sh && flash.bat`）。
2. COM21 串口监视（115200 8N1）看上电日志与 JTAG 命令流。
3. 按 §8.1 接好 JTAG 五线 + GND（目标需独立上电）。
4. 跑 OpenOCD（强制 JTAG transport）：
   ```bash
   openocd -f interface/cmsis-dap.cfg -c "transport select jtag" -f target/stm32h7x.cfg
   ```
   或写一份 `probe_jtag.cfg`：
   ```tcl
   source [find interface/cmsis-dap.cfg]
   transport select jtag
   source [find target/stm32h7x.cfg]
   ```
   然后 `openocd -f probe_jtag.cfg`。
5. 预期：COM21 出现 `>> req cmd=0x02`（Connect/JTAG）→ `DAP cmd id=0x15`
   （JTAG_Configure）→ `id=0x16`（IDCODE，返回 0x... 非零 IDCODE）→ `id=0x17`（Transfer，
   读 DP IDCODE / 上电 CTRLSTAT）→ 连上目标。若读到的 IDCODE 为 `0x00000000` 或 `0xFFFFFFFF`
   说明 TDO 未接好或目标未上电。

### 8.3 顺带修复（SWD 也受益）

`components/swd/include/swd.h` 的 `SWD_REQ` 宏地址位构造有 bug：原写法
`((addr8 >> 2) & 0x0Cu)` 恒为 0，导致除 IDCODE(0x00) 外的所有 DP 寄存器访问（CTRLSTAT/SELECT/
RDBUFF/ABORT）都错映射到地址 0x00，SWD 下只有 IDCODE 能读成功。已改为 `(addr8 & 0x0Cu)`，
A[3:2] 正确映射到 request 位 3/2。该修复同时提升 SWD 模式下的 DP/AP 读写正确性。

