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
```

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
