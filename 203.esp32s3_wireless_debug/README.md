# ESP32-S3 CMSIS-DAP 无线调试探针

基于 ESP32-S3 的 CMSIS-DAP v1（HID）调试探针固件，支持 **SWD** 与 **JTAG** 两种模式，
并预留外部 PSRAM 为后续 **WiFi 无线调试（DAP-over-WiFi）** 做准备。

---

## 1. 硬件规格（esp32s3 **N16R8**）

| 资源 | 规格 | 说明 |
|------|------|------|
| SoC | ESP32-S3 (Xtensa LX7, 双核) | 主核跑探针逻辑，次核留给 WiFi |
| 内置 RAM | **512 KB** | IRAM / DRAM；位带热点代码驻留 IRAM |
| 外置 Flash | **16 MB** (Quad SPI, 80 MHz) | 固件 + 分区表（factory 当前 2 MB，余量充足） |
| 外置 PSRAM | **8 MB** (Octal, OPI) | 已启用；预留池供无线调试传输层使用 |
| USB | 原生 USB OTG (GPIO19/20, FS) | CMSIS-DAP HID 通道 |
| 日志串口 | CH340 (GPIO43/44, 115200 8N1) | 仅引导期与错误日志，热路径日志默认关闭 |
| 默认 CPU | **240 MHz** | 拉满以保证位带时序稳定 |

> 早期调试实测：SWD/JTAG 在 100 kHz 飞线已调通并可用于仿真下载；
> 性能优化（CPU 240 MHz + 热点 IRAM + GPIO 驱动强度拉满 + 热路径日志关闭）
> 目标将稳定工作频率提升到 MHz 级、下载/仿真速度提升约 10 倍。

---

## 2. 调试引脚分配

JTAG 模式**不切换** SWD 引脚：TCK/TMS 与 SWCLK/SWDIO 物理复用，
仅新增 TDI/TDO/nTRST 三个引脚。

| 信号 | GPIO | 方向 | 备注 |
|------|------|------|------|
| SWCLK / TCK | **GPIO4** | 输出 | 共用，推挽 |
| SWDIO / TMS | **GPIO5** | 双向 | 共用，上拉，空闲高 |
| TDI | **GPIO7** | 输出 | JTAG 新增 |
| TDO | **GPIO8** | 输入 | JTAG 新增，上拉 |
| nTRST | **GPIO9** | 开漏 | JTAG 新增，默认释放 |
| nRESET | **GPIO6** | 开漏 | SWD/JTAG 共用，默认释放 |

**与目标（如 STM32）对接：**
| 探针 | STM32 (SWD) | STM32 (JTAG) |
|------|-------------|--------------|
| SWCLK/TCK (4) | SWCLK (PA14) | TCK (PA14) |
| SWDIO/TMS (5) | SWDIO (PA13) | TMS (PA13) |
| TDI (7) | — | TDI (PA15) |
| TDO (8) | — | TDO (PB4) |
| nTRST (9) | — | nTRST (PB3，可选) |
| nRESET (6) | nRESET | nRESET |

> 飞线连接时先用 **100 kHz** 验证链路，再逐步提高时钟（软件上限已放宽到 8 MHz）。

---

## 3. 快速开始

```bash
# 1) 编译（自动载入 env.sh 内的 ESP-IDF v6.1 环境）
./build.sh              # 增量
./build.sh clean        # 清空重编

# 2) 烧录（CH340 日志口，如 COM21）
./flash.sh

# 3) 连接测试（OpenOCD 通过 CMSIS-DAP HID 识别探针）
openocd -f interface/cmsis-dap.cfg -f target/stm32h7x.cfg
#    JTAG 模式需显式指定： -c "transport select jtag"
```

> 日常下载 / 仿真用 **SWD**（默认 transport，速度正常）；JTAG 模式仅建议用于 chain / IDCODE 发现、
> 多 TAP examine、边界扫描——其 flash 下载受 CMSIS-DAP v1 HID 限制明显慢于 SWD（见 §7.2）。

详细的协议层、OpenOCD/Keil 测试命令、排错清单见 **doc/useage.md**。

---

## 4. 性能与稳定性优化（已落地）

| 优化项 | 做法 | 收益 |
|--------|------|------|
| CPU 频率 | `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240` | 位带延迟环余量 +50% |
| 热点 IRAM | `swd_transfer/sequence`、`jtag_ir/transfer/...` 标 `IRAM_ATTR` | 消除 flash cache miss 抖动，时钟可稳定到 MHz |
| GPIO 驱动 | `gpio_set_drive_capability(..., GPIO_DRIVE_CAP_3)` | 飞线高速信号完整性 |
| 热路径日志 | HID 收发包、`dap_task` 每包请求改为 `ESP_LOGV/LOGD`；默认日志级 `WARN` | 下载时不因串口日志拖速 |
| DAP 任务 | 绑核 1、优先级 20（高于 USB 任务 10，低于 WiFi 核心任务） | 位带期间不被 WiFi 中断抢占 |
| 时钟上限 | SWD/JTAG `MAX_CLOCK_HZ` 4 MHz → **8 MHz** | 主机可请求更高速度 |

---

## 5. RAM / PSRAM 预算（无线调试预留）

- 内部 RAM(512 KB)：IRAM 存放位带热点代码；DRAM 给 USB/WiFi 协议栈与运行栈。
- 外置 PSRAM(8 MB)：通过 `CONFIG_WIRELESS_DEBUG_RESERVE_RAM`（默认开）在启动期
  从 PSRAM `heap_caps_malloc(MALLOC_CAP_SPIRAM)` 划分一块**静态保留池**
  （`CONFIG_WIRELESS_DEBUG_RESERVE_RAM_SIZE_KB`，默认 512 KB），**永不释放**，
  供后续 DAP-over-WiFi 包缓冲使用，避免与内部稀缺 DRAM 竞争。
- WiFi 缓冲已适度收紧（`STATIC_RX=6 / DYNAMIC_RX=16 / DYNAMIC_TX=16`）以腾出内部 DRAM。

---

## 6. 目录结构

```
components/
  usb_device/       TinyUSB 设备 + HID 数据通路
  cmsis_dap/        CMSIS-DAP v1 命令处理（SWD/JTAG 分发）
  debug_engine/     Cortex-M 调试引擎（halt/run/step/读写寄存器/内存）
  swd/              SWD 位带引擎（寄存器级 GPIO）
  jtag/             JTAG 位带引擎（寄存器级 GPIO，逐字移植 ARM JTAG_DP.c）
main/               app_main 入口 + 整包 whole-archive 链接
doc/useage.md       详细使用与排错文档
```

---

## 7. 已知限制与排错要点

### 7.1 传输路径选型
- **SWD（推荐）**：flash 下载 / 仿真 / 日常调试走 SWD。SWD 引擎已修复（clock 实测校准、11-stage
  under-reset connect、Attach/Launch 工作流），下载 / 仿真速度与 ST-Link 同量级（3–5 s）。
- **JTAG**：用于 chain / IDCODE 发现、多 TAP examine、边界扫描。`Invalid ACK (4)` FAULT 已修复（见 §7.3），
  但 **flash 下载慢是已知协议限制，暂不处理**（见 §7.2）。

### 7.2 JTAG flash 下载慢（已知限制，暂不处理）
根因：**CMSIS-DAP v1 over HID** 的 IN 端点靠主机 ~1 kHz 轮询拉取 → 每条 DAP 命令下限延迟 ≈ 1 ms
（HID 规范硬约束，固件无法绕过）。OpenOCD 的 JTAG 传输每写一个 32 位字就发一条
`DAP_JTAG_Sequence(0x14)` 并等 ACK，66 KB 镜像 ≈ 1.8 万条命令 ≈ 18 s（实测全程 21.3 s）。
- **与时钟无关**：4 MHz 已达标，再提速无益。
- ST-Link 3–5 s 对比：ST-Link 走 bulk / high-throughput 通道 + JTAG 批量传输优化。
- 可选提速（均未实施）：① 加 CMSIS-DAP v2（WinUSB Bulk，512 B 包，命令率提 3–10 倍）；
  ② 改用 `DAP_JTAG_Transfer(0x17)` 批量路径（需改 OpenOCD 驱动，固件已支持并验证）。
- **结论**：日常下载 / 仿真请用 **SWD**；JTAG 仅作链路 / 边界扫描用途。

### 7.3 JTAG `Invalid ACK (4)` FAULT（已修复）
曾出现"OpenOCD 能发现 TAP、IDCODE 正确，但后续任何 DAP 访问全 FAULT"。最终根因：JTAG 引脚宏
`PIN_TDI_OUT(v)` 把整字节传进 `bool` 形参 `pin_out()`，C 的非零→true 提升使 TDI 在字节非零时恒高，
把 DPACC IR(0x0A) 错移成 BYPASS(0x0F)。修复：宏内掩码 `& 1U`
（`pin_out(TDI_GPIO, (((v) & 1U) != 0U))`）。另按 ARM 官方 `JTAG_DP.c` 逐位修正：Shift-IR 末位须与
`TMS=1` 同边沿（补一拍即多移一位）、去除多余 `after` deskew、ABORT 改用独立 IR `0x08`。
注意 OpenOCD 实际只走 `DAP_JTAG_Sequence(0x14)`（非 `DAP_JTAG_Transfer(0x17)`）。
完整排错方法论见 skill **`stm32-cmsis-dap-probe`**。

### 7.4 调试注意事项
- **双主机警告**：同一目标不能同时挂 J-Link + 本探针，否则出现确定性坏值（如 DPIDR `0xff4c001b`）；
  VSCode 调试前先断开 Keil / J-Link 会话。
- **空 Flash 假故障**：目标板 Flash 为空时 `mdw 0x08000000` 全 `ffffffff` → Cortex-M lockup
  （`pc=0xfffffffe`），属目标侧现象，非探针缺陷；用 `reset halt` + RAM 写读回环判定探针是否健康。
- **验收链（逐级排除）**：① HID 直发 `tools/run_jtag_transfer.py`（期望 IDCODE0 = `0x6BA00477`、
  各次 `ack=1 OK`）；② OpenOCD init（两 TAP 识别 + `Examination succeed` + `Cortex-M7` 检测）；
  ③ `reset halt`（`halted due to debug-request`）；④ RAM 写读回环 `mww 0x24000000 0xDEADBEEF` +
  `mww 0x24000004 0x12345678` → `mdw` 读回 `deadbeef 12345678 ...`。
