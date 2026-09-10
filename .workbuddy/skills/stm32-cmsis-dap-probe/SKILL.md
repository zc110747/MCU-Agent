---
name: stm32-cmsis-dap-probe
description: 自研 CMSIS-DAP / SWD / JTAG 调试探针固件（STM32H7 + TinyUSB，或 ESP32-S3 + ESP-IDF + TinyUSB HID）的移植、参数调优与主机侧验收：SWD 位带时序（DWT 周期计数替代 Keil 内联汇编、IRAM_ATTR 热点、GPIO 驱动强度）、v1 over HID 的 1 kHz 轮询协议限制（JTAG 下载慢不可救，日常用 SWD）、USB/Wi-Fi 仲裁必须用 tud_mounted 而非 tud_connected、VDD33USB 供电导致 USB 不枚举、双主机同时挂目标读出确定性坏值、空 Flash 导致 Cortex-M lockup 的假故障、逐级验收链。适用于“自研 DAP 探针”“OpenOCD 找不到 CMSIS-DAP”“JTAG Invalid ACK(4) FAULT”“下载速度慢”“无线调试探针”“探针 USB 不枚举”。触发词：CMSIS-DAP、DAP v1、SWD 位带、JTAG_DP、IRAM_ATTR、tud_mounted、VDD33USB、OpenOCD verify cfg、IDCODE 0x6BA00477、DPIDR lockup、DAP-over-WiFi、ESP32-S3 探针、DWT CYCCNT 延时。
agent_created: true
---

# 自研 CMSIS-DAP / SWD / JTAG 调试探针

把一块 MCU 变成可被 OpenOCD / Keil / VSCode 直接使用的 **CMSIS-DAP 调试探针**的实操配方。
本仓两条已验证实现：

| 平台 | 项目 | 传输 | 特点 |
|---|---|---|---|
| **STM32H743 + TinyUSB**（无 RTOS / FreeRTOS） | `007.stm32h743_cmsis_dap` | USB FS HID/Bulk | 已端到端验证：用本探针给 **STM32F429** 下载与仿真 |
| **ESP32-S3 N16R8 + ESP-IDF + TinyUSB** | `203.esp32s3_wireless_debug` | USB FS HID + **Wi-Fi 兜底** | SWD/JTAG 双模，热点 `ESP32-DAP-XXXX` @192.168.4.1 TCP 50000 |

## 一、何时使用
- 从零实现 CMSIS-DAP v1 固件，或把 ARM 官方 `CMSIS-DAP` 源码移植到 GCC / Cortex-M7 / Xtensa。
- OpenOCD 报 `CMSIS-DAP not found` / `Invalid ACK (4) FAULT` / `Error connecting DP`。
- 探针能枚举但下载奇慢，或 JTAG 可用而 SWD 不可用（反之亦然）。
- 做 **DAP-over-WiFi**（无线调试）时的 USB / Wi-Fi 仲裁设计。

## 二、SWD 位带时序（最容易做错的地方）

### 2.1 延时不能用 Keil 内联汇编，改用 DWT 周期计数
ARM 官方 `DAP.h` 的 `PIN_DELAY_SLOW` 用 `__asm { SUBS / BNE }` 循环，假设每轮恰好
`DELAY_SLOW_CYCLES` 个周期。**在 Cortex-M7 上该假设不成立**（双发射 + I-Cache + 分支折叠），
猜低了会让 SWCLK **比请求值更快**——这是危险方向（目标采样不过来）。
- 改法：基于 **DWT CYCCNT** 的精确自旋（`DAP.h` 内），`DELAY_SLOW_CYCLES` 置 `1U`，
  `delay` 语义改为「核心周期数」。
- DWT 必须在 `DAP_SETUP()` 中**解锁并使能**（`DEMCR.TRCENA=1` + `DWT->CYCCNT=0` + `DWT->CTRL|=1`）。
- **`DAP_SWJ_Clock()` 的 `delay` 在 fast_clock 分支未初始化**（ARM 原版潜在 UB）：自行初始化为 `1U`。

### 2.2 CPU / 时钟与 `CPU_CLOCK` 必须一致
`DAP_config.h` 的 `CPU_CLOCK` 必须与实际 SYSCLK 一致，SWCLK 延时由它导出。
- STM32H7：HSE 25 MHz → PLL1 ×N64 ÷P2 → SYSCLK **400 MHz**（HCLK 200 / APB 100）。
- ESP32-S3：CPU 拉到 **240 MHz**（`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`）以保证位带时序余量 +50%。
- USB 48 MHz：STM32H7 侧建议 **HSI48 + CRS**（主机 SOF 自动 trim，免外部晶振，满足 FS 0.25% 精度）。

### 2.3 热点代码放 IRAM + GPIO 驱动强度拉满（ESP32-S3 实测）
```c
void IRAM_ATTR swd_transfer(...) { /* 位带热点 */ }        // 消除 flash cache miss 抖动
gpio_set_drive_capability(gpio, GPIO_DRIVE_CAP_3);          // 飞线高速信号完整性
```
- 热路径日志降级为 `ESP_LOGV/LOGD`，默认日志级设 **`WARN`**（否则 UART 日志成为下载瓶颈）。
- DAP 任务绑核 + 高优先级（如核 1、优先级 20，高于 USB 任务），避免位带期间被 Wi-Fi 抢占。
- 综合后 SWD 时钟上限可由 4 MHz 提到 **8 MHz**。

## 三、JTAG 踩坑（比 SWD 更容易出错）

### 3.1 `Invalid ACK (4)` FAULT：99% 是位取值被 C 隐式提升
现象：OpenOCD 能发现 TAP、IDCODE 正确，但**后续任何 DAP 访问全 FAULT**。
根因案例：`PIN_TDI_OUT(v)` 把整字节传进 `bool` 形参 `pin_out()`，C 的「非零→true」提升
使 TDI 在字节非零时**恒高**，把 DPACC IR(`0x0A`) 错移成 BYPASS(`0x0F`)。

**修复**：宏内必须掩码 `& 1U`：
```c
pin_out(TDI_GPIO, (((v) & 1U) != 0U));
```

### 3.2 其余按 ARM 官方 `JTAG_DP.c` 逐位校正的要点
- **Shift-IR 末位须与 `TMS=1` 同边沿**（补一拍即多移一位）。
- 去除多余的 `after` deskew。
- ABORT 用**独立 IR `0x08`**。
- OpenOCD 实际只走 **`DAP_JTAG_Sequence(0x14)`**，不是批量路径 `DAP_JTAG_Transfer(0x17)`。

## 四、CMSIS-DAP v1 over HID 的硬限制（架构决策必须先知）

**IN 端点靠主机 ~1 kHz 轮询拉取 → 每条 DAP 命令下限延迟 ≈ 1 ms**，这是 HID 规范硬约束，
固件无法绕过。后果：
- OpenOCD 的 JTAG 传输每写一个 32 位字就发一条命令并等 ACK → 66 KB 镜像 ≈ 1.8 万条 ≈ **18 s**（实测 21.3 s）。
- **与时钟无关**：4 MHz 已达标，再提速无益。ST-Link 3–5 s 是因为走 bulk 通道 + JTAG 批量优化。

**结论与对策**：
- **日常下载 / 仿真一律用 SWD**（修好后与 ST-Link 同量级 3–5 s）；JTAG 仅用于 chain/IDCODE 发现、
  多 TAP examine、边界扫描。JTAG flash 下载慢属已知协议限制，不必优化。
- 真要提速的可选路线（均未必需）：① 实现 **CMSIS-DAP v2**（WinUSB Bulk，512 B 包，命令率提 3–10 倍）；
  ② 走 `DAP_JTAG_Transfer(0x17)` 批量路径（需改 OpenOCD 驱动）。

## 五、USB / Wi-Fi 仲裁（无线探针专属，极易踩）

**政策：两种传输互斥，不同时运行。** 上电先初始化 USB，在
`CONFIG_WIFI_DAP_USB_WAIT_MS`（默认 **3000 ms**）窗口内等主机枚举：
- 检测到 USB 主机 → 常驻 USB 模式，**完全不启动 esp_wifi**（不开热点）；
- 窗口内无主机 → 才启动 SoftAP + TCP 50000。

**为什么必须互斥**：ESP32-S3 上 Wi-Fi RF 一旦工作就会破坏 USB OTG HID 时序，
主机侧表现为 CMSIS-DAP 通讯错误 / 命令超时。**无线只能作「供电但无 USB 主机」时的兜底。**

### 5.1 判定主机存在必须用 `tud_mounted()`，不能用 `tud_connected()`
⚠️ **高频致命坑**：ESP32-S3 上 `tud_connected()` 只代表 **VBUS 存在**
——插充电头 / 充电宝也会为真，会被误判成"有 USB 主机"而**永远不开热点**。
```c
bool usb_device_is_connected(void) { return tud_mounted(); }   // 正确
// tud_connected() 仅作 vbus 诊断保留
```

### 5.2 Kconfig 宏不能用于组件 CMakeLists 的 `if()`
`CONFIG_DEBUG_ENABLE_WIFI` 之类的宏**只能**用于 C 代码里的 `#if` 门控；
**不能**写进 `main/CMakeLists.txt` 的 `if(CONFIG_...)`——本 IDF 版本在组件注册阶段取不到该变量，
条件化 `REQUIRES` 会静默丢掉 include 路径并编译失败。详见 `esp-idf-whole-archive-link`。

### 5.3 内存预算
- 内部 RAM(512 KB)：IRAM 放位带热点；DRAM 给 USB/Wi-Fi 协议栈与运行栈。
- 外置 PSRAM(8 MB)：启动期 `heap_caps_malloc(MALLOC_CAP_SPIRAM)` 划一块**静态保留池**
  （默认 512 KB，**永不释放**）供 DAP-over-WiFi 包缓冲，避免与内部 DRAM 竞争。
- Wi-Fi 缓冲适度收紧（`STATIC_RX=6 / DYNAMIC_RX=16 / DYNAMIC_TX=16`）腾出内部 DRAM。

## 六、主机侧验收（逐级排除，别一次性猜）

1. **USB HID 链路体检**（不需接目标板）：`openocd -f tools/verify_usb_hid.cfg`
   → 见到 `CMSIS-DAP: FW Version = 1.2.0` / `Interface ready` 即链路正常；
   末尾 `Error connecting DP` 只是当前没挂目标，属预期。
2. **HID 直发裸命令**：跑脚本期望 **IDCODE0 = `0x6BA00477`**（H7 侧身份参考），不经过 OpenOCD 协议层。
3. **接目标跑 OpenOCD**：`transport select swd` → `reset halt` → `mdw` 读回
   （H743 DPIDR=`0x6ba02477`，F429 DPIDR=`0x2ba01477`）。
4. **热点是否广播**（无线模式）：`netsh wlan show networks mode=bssid | findstr ESP32-DAP`
   （USB 模式下应为空；Windows 扫描有缓存，拔线后等 20–30 s 再查）。

## 七、常见主机侧假故障

| 现象 | 真因 | 处置 |
|---|---|---|
| **双主机同时挂目标**，读出确定性坏值（如 DPIDR `0xff4c001b`） | J-Link + 自研探针同时接同一目标 | VSCode/OpenOCD 调试前先断开 Keil / J-Link 会话 |
| 目标 **Flash 为空** 时 `mdw 0x08000000` 全 `ffffffff` → Cortex-M lockup（`pc=0xfffffffe`） | 目标侧现象，**非探针缺陷** | 用 `reset halt` + RAM 写读回环判定探针是否健康；先给目标烧个正常固件 |
| **STM32H7 USB 完全不枚举**（主机提示"未检测到 USB 设备"） | FS PHY 的 **VDD33USB 未供电** | 使能 VDD33USB 稳压器（`HAL_PWREx_EnableUSBVoltageDetector()` 相关配置），见 `stm32-peripheral-drivers` |
| 分不清「固件没跑」还是「USB 没上电」 | 缺上电心跳 | 加心跳 LED / 串口 banner 先行确认固件已启动 |
| 日志级设为 `WARN` 后看不到 `ESP_LOGI` | **有意为之**（UART 日志不能成 HID 下载瓶颈） | 关键仲裁结果用 `ESP_LOGW` 输出以便可见 |

## 八、相关技能
- `esp-idf-windows-build` / `esp-idf-whole-archive-link`：ESP-IDF 侧构建与链接（weak 覆盖、整包链接）。
- `esp32-cortex-debug`：VSCode + OpenOCD + xtensa-gdb 的主机侧调试链路配置。
- `stm32-peripheral-drivers`：USB OTG_FS VDD33USB 供电、GPIO/USART 复用配置。
- `stm32-swd-forensics`：目标侧 SWD 取证（`halt` + `wait_halt` + `mdw` 的正确顺序）。
