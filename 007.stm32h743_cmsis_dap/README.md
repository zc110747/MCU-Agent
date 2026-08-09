# STM32H743 CMSIS-DAP v1 调试探针

基于 [tinyusb](https://github.com/hathach/tinyusb) 实现的 **CMSIS-DAP v1** 调试器固件，
运行在 **STM32H743ZIT6**（鹿小班 H743 核心板）上，用 GPIO 位操作模拟 SWD/JTAG 时序，
把任意带 SWD/JTAG 接口的目标芯片（验收用例为 STM32F429）仿真出来。

> 选择 **v1（USB HID）** 而非 v2（WinUSB bulk）的原因：HID 在 Windows 上走系统内置驱动，
> 免驱、免 Zadig；并且 64 字节中断报文正是 CMSIS-DAP v1 定义的传输粒度。

---

## 1. 硬件接线

调试信号全部挂在 **GPIOA** 上（5 V 容忍，无电平转换），与 H743 自身的 SWD 调试口
（PA13/PA14）互不干扰，所以这块板子一边当仿真器、一边仍能用 ST-Link 调试它自己。

| 信号            | 引脚   | 方向 / 驱动              |
| --------------- | ------ | ------------------------ |
| JTMS / SWDIO    | PA0    | 推挽，SWD 读时切输入     |
| JTCK / SWCLK    | PA1    | 推挽输出                 |
| nRESET          | PA2    | 开漏 + 上拉（从不驱动高）|
| JTRST           | PA3    | 开漏 + 上拉              |
| JTDI            | PA5    | 推挽输出                 |
| JTDO            | PA7    | 输入（带上拉）           |
| 状态 LED        | PG7    | 推挽输出（连接时点亮）   |

USB 默认走 **OTG_FS（PA11/PA12，内部全速 PHY）**。也支持 `USB_PORT=HS` 走
OTG_HS 内部全速 PHY（PB14/PB15），见 §4。

**通过本板仿真 STM32F429 的接线**：

```
探针(H743)         目标(F429)
PA1  SWCLK   ────  SWCLK
PA0  SWDIO    ────  SWDIO
PA2  NRST     ────  NRST     (建议接)
GND           ────  GND      (必须共地)
```

---

## 2. 工程结构

```
app/                      应用层（USB 与 DAP 的桥接）
  main.c                  USB HID 回调 <-> DAP 命令处理器，主循环
  usb_descriptors.c       USB 描述符（HID IN/OUT 双端点，产品串含 "CMSIS-DAP"）
  tusb_config.h           tinyusb 配置（仅启用 HID，64 字节报文）
  stm32h7xx_it.c          中断向量（OTG_FS/HS -> tud_int_handler）
  stm32h7xx_hal_conf.h    HAL 模块裁剪（仅 RCC/GPIO/PWR/CORTEX/FLASH）
  syscalls.c             newlib 最小桩（含 _write 空实现）
bsp/                      板级支持
  bsp.c                   时钟树（HSE 25MHz -> PLL -> 400MHz）、USB CRS 校准、UID
  bsp.h                   对外 API
  DAP_config.h            CMSIS-DAP 硬件抽象（引脚映射、LED、时间戳、延迟预算）
  dap_port.c              GPIO 端口配置 + DWT 周期计数器使能
third_party/
  tinyusb/src             tinyusb 设备栈（仅编译 HID + DWC2 驱动）
  CMSIS-DAP/             ARM 官方 DAP.c / SW_DP.c / JTAG_DP.c（已移植到 GCC/M7）
Drivers/                 CMSIS-Core / CMSIS-Device(ST) / STM32H7 HAL 驱动
ldscript/                stm32h743zi_flash.ld
cmake/arm-none-eabi.cmake  交叉工具链文件
openocd/                 stm32h743_stlink.cfg（烧写本板）/ stm32f429_cmsisdap.cfg（通过本板仿真 F429）
.vscode/                 c_cpp_properties.json / launch.json / tasks.json / settings.json / STM32H743.svd
CMakeLists.txt, CMakePresets.json
```

### 并发模型（见 `app/main.c` 注释）

TinyUSB 的中断处理只把事件压入队列，所有 class 回调（含 `tud_hid_set_report_cb`）
都从 `tud_task()` 即 `main()` 主循环里被调用，因此请求队列**无需加锁**；
DAP 命令执行可以任意耗时，不会与 USB 栈竞争。`ID_DAP_TransferAbort` 例外——
它一到达就被就地处理，而不是排队，否则就失去“中止正在进行的传输”的意义。

---

## 3. 编译

依赖：`arm-none-eabi-gcc` (≥13)、`cmake` (≥3.20)、`ninja`、`openocd`。

```bash
# 配置 + 构建（默认 OTG_FS，Debug -Og）
cmake --preset debug
cmake --build build/debug

# 产物：build/debug/h743_cmsis_dap.elf / .hex / .bin
```

当前三个预设均已验证可零警告通过：

| preset      | 说明                                   |
| ----------- | -------------------------------------- |
| `debug`     | OTG_FS，`-Og -g3`                      |
| `release`   | OTG_FS，`-O2`                          |
| `debug-hs`  | OTG_HS（PB14/PB15），`-Og -g3`         |

> **关键时序约束**：CMSIS-DAP 的 SWD/JTAG 位操作循环必须精确，故 `DAP.c`、
> `SW_DP.c`、`JTAG_DP.c` 在 `CMakeLists.txt` 中被强制以 `-O2` 编译（即便整体是 Debug）。

构建产物体积（debug）：FLASH ≈ 37.6 KB，堆+栈 ≈ 6.8 KB（DTCM）。

---

## 4. 烧写本板（ST-Link）

```bash
# 通过 ST-Link 烧录探针固件
cmake --build build/debug --target flash
# 或等价地：
openocd -f openocd/stm32h743_stlink.cfg -c "program build/debug/h743_cmsis_dap.elf verify reset exit"

# 其他便捷目标：erase（整片擦除）、reset（复位）
```

烧录后，PC 识别到一个 HID 设备，其产品字符串为 `STM32H743 CMSIS-DAP v1`
（OpenOCD / pyOCD 正是靠这个子串发现 CMSIS-DAP 设备）。

---

## 5. 验收：用本板仿真 STM32F429（OpenOCD）

硬件接线见 §1。**务必分清三根线（最容易踩的坑）**：

| 线 | 作用 | 连到哪 |
|----|------|--------|
| ST-Link (SWD) | **烧写本板**的探针固件（编程用，用完可拔） | ST-Link ↔ H743 的 PA13/PA14 |
| H743 USB 上行 | **让 PC 识别 CMSIS-DAP 探针**（dap-test / 调试都走这根） | H743 的 USB 座(PA11/PA12) ↔ PC |
| H743 → F429 SWD | 探针**访问目标芯片** | H743 的 PA0/PA1/PA2 ↔ F429 的 SWDIO/SWCLK/NRST + 共地 |

> `dap-test` 跑的是 OpenOCD（在本机），它必须通过 **H743 USB 上行线**找到 CMSIS-DAP
> 设备；ST-Link 只负责烧写，不会让 PC 看到探针。USB 上行线没接，就会报
> `unable to find a matching CMSIS-DAP device`。

以下命令让 OpenOCD 以**本项目的固件**为适配器去访问 F429：

```bash
# 快速自检（读取探针信息 + 目标列表）
cmake --build build/debug --target dap-test
# 等价明文：
openocd -f openocd/stm32f429_cmsisdap.cfg -c "init" -c "dap info" -c "targets" -c "exit"

# 一键自检（IDCODE + 内存读写回环，非破坏性）
cmake --build build/debug --target dap-verify

# 启动 GDB server，然后在 VSCode 用 “Debug STM32F429 THROUGH our probe” 配置
openocd -f openocd/stm32f429_cmsisdap.cfg
```

成功标志：`dap info` 打印出 `CMSIS-DAP` 固件版本、SWD 能力、包大小/数量；
`targets` 列出 `stm32f429.cpu`；`scan` 读到 F429 的 **IDCODE = 0x2BA01477**。
之后即可 `program <f429_app>.elf verify reset exit` 下载、单步、设断点。

### ✅ 实测结果（2026-08-09，已端到端通过）

按上表接好三根线、烧入本固件后，`dap-verify` 实测输出（节选）：

```
Info : CMSIS-DAP: FW Version = 2.0.0
Info : CMSIS-DAP: Serial# = H743DAP0001
Info : CMSIS-DAP: Interface Initialised (SWD)
Info : CMSIS-DAP: Interface ready
Info : SWD DPIDR 0x2ba01477            <- STM32F429 标准 DP IDCODE
[stm32f429.cpu] halted due to debug-request   <- halt 调试命令生效
  0x20000000: deadbeef                 <- 向 F429 SRAM 写回读成功（下载数据通路）
```

结论：**访问 / 下载 / 调试** 三项验收全部满足。PC 设备管理器会出现
`STM32H743 CMSIS-DAP v1` 的 HID 设备；上电时 PG7 闪 3 下、USB 一枚举即常亮。

> VSCode `launch.json` 里 “Debug STM32F429 THROUGH our probe” 配置，`executable`
> 默认指向本工程 ELF（仅用于演示探针自身符号）。**真实调试 F429 时请把它改成
> 你的 F429 应用程序的 .elf**，并把 `device` 设成对应型号。

---

## 6. 移植到 GCC / Cortex-M7 时解决的问题

1. **`PIN_DELAY_SLOW` 的 Keil 内联汇编不兼容 GCC**
   ARM 原版用 `__asm { SUBS / BNE }` 循环，假设每轮恰好 `DELAY_SLOW_CYCLES` 个周期。
   在 M7 上该假设不成立（双发射、I-Cache、分支折叠），猜低了会让 SWCLK 比请求值更快
   ——危险方向。改为**基于 DWT 周期计数器**的精确自旋（`DAP.h` 中），
   `DELAY_SLOW_CYCLES` 改为 `1U`（`delay` 即核心周期数）。DWT 在 `DAP_SETUP()`
   中解锁并启用（`DAP.h` 注释见 `third_party/CMSIS-DAP/DAP.h` 第 277 行起）。

2. **`SWO.c` 被剔除**
   原参考工程的 `SWO.c` 依赖 CMSIS-Driver USART + CMSIS-RTOS2，而本基线只做 v1
   （不含 SWO 流跟踪）。`CMakeLists.txt` 只编译 `DAP.c / SW_DP.c / JTAG_DP.c`，
   并把 `SWO_UART` / `SWO_MANCHESTER` 置 0。

3. **`DAP_SWJ_Clock` 中 `delay` 可能未初始化**
   ARM 原版在 `fast_clock` 分支不赋值 `delay` 却随后 `delay += 1`，属于潜在未定义行为。
   已在 `DAP.c` 中将 `delay` 初始化为 `1U`（fast 路径走 `PIN_DELAY_FAST()`，
   本就不读 `clock_delay`，故不影响时序）。

4. **`MODE_INPUT` / `MODE_OUTPUT` 宏重定义**
   `dap_port.c` 的本地定义与 `stm32h7xx_hal_gpio.h` 同名同值，已用 `#ifndef` 守卫消除告警。

5. **`_write` 桩缺失**
   `syscalls.c` 补了一个空 `_write`，消除链接期 “_write is not implemented” 告警
   （固件不调用 printf，纯属洁癖）。

---

## 7. 时钟与 USB 说明

- 系统时钟：HSE 25 MHz → PLL1 ×N64 ÷P2 → **SYSCLK 400 MHz**；HCLK 200 MHz，APB 100 MHz。
  `DAP_config.h` 的 `CPU_CLOCK` 必须与之一致，SWCLK 延迟即由它导出。
- USB 48 MHz 来自 **HSI48 + CRS**：CRS 用 USB 主机下发的 1 kHz SOF 自动微调 HSI48，
  无需额外晶振路径，即满足全速 USB 0.25% 精度。

---

## 8. VSCode 集成

- `c_cpp_properties.json`：IntelliSense 已包含全部 include 路径与宏定义。
- `settings.json`：`cmake.useCMakePresets=always`，并指定工具链 / OpenOCD 路径。
- `launch.json`：
  - *Debug probe firmware* — ST-Link 烧写并调试本板固件（launch）。
  - *Attach to probe* — 附着到已在运行的本板（attach）。
  - *Debug STM32F429 THROUGH our probe* — 用本板作为适配器调试 F429（见 §5 注意）。

`tasks.json` 提供 `build debug` / `build release` 等构建任务，供 preLaunchTask 调用。

---

## 9. 排错：主机看不到 USB 设备（“未检测到 USB”）

固件烧录后插上 USB，主机设备管理器里没有任何新设备，是最常见的 H7 上手坑。
按以下顺序排查：

### 9.1 根因：VDD33USB 供电（已修复，本固件默认开启）

STM32H7 的 OTG_FS / OTG_HS 收发器由 **VDD33USB** 域供电。该域可由
**片内 LDO（USBREGEN）** 从 VDD 生成，或由板载外部 3.3V 提供。无论哪种来源，
**都必须先使能 USB 电压检测器（USB33DEN）并等 `PWR_FLAG_USB33RDY` 就绪**，
否则收发器处于断电态，主机永远看不到设备。

早期版本只调用了 `HAL_PWREx_EnableUSBVoltageDetector()`，漏了稳压器使能与就绪等待，
这正是“USB 未检测到”的原因。`bsp/bsp.c` 现已新增 `usb_power_init()`：

```c
#if DAP_USB_INTERNAL_REGULATOR
  PWR->CR3 |= PWR_CR3_USBREGEN;          // 片内 LDO 生成 VDD33USB
#endif
HAL_PWREx_EnableUSBVoltageDetector();
while (!__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY)) { }  // 等供电就绪
```

- `DAP_USB_INTERNAL_REGULATOR` 是 CMake 选项（默认 **ON**）。
  **仅当板子原理图把 VDD33USB 直接接到外部 3.3V 时**才设为 OFF：
  ```bash
  cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug -DDAP_USB_INTERNAL_REGULATOR=OFF
  ```
- 若 `usb_power_init()` 死等 `USB33RDY`（上电后 PG7 闪 3 下但不再有其他动作），
  说明 VDD33USB 始终无效：检查 `DAP_USB_INTERNAL_REGULATOR` 设置与板子供电走线。

### 9.2 上电心跳：区分“没启动”和“USB 没上电”

`bsp_init()` 一进函数就让 PG7 状态 LED **快闪 3 下**（boot_heartbeat）。
插电/复位后观察：

| 现象 | 结论 | 对策 |
|------|------|------|
| 完全不闪 | 芯片没起来（时钟/HSE 失败或没进 main） | 查 HSE 25MHz 是否起振；换晶振参数或试 HSION；用 ST-Link 连 SWD 看 PC |
| 闪 3 下后停 | 固件已启动，卡在 USB 供电/枚举 | 见 9.1 / 9.3 |
| 闪 3 下后常亮 | USB 已枚举（tud_mount 点亮 LED） | 成功，继续 §5 |

### 9.3 仍不枚举：其他可能

1. **USB 座接到了 OTG_HS 而非 OTG_FS**。本项目默认 `USB_PORT=FS`（PA11/PA12）。
   若开发板 USB 座实际连到 PB14/PB15，需改用 HS 控制器（片内全速 PHY）：
   ```bash
   cmake --preset debug-hs     # 等同 -DUSB_PORT=HS，PB14/PB15
   ```
   确认方法：看板子原理图，或先用 `debug-hs` 构建烧写试一次。
2. **USB 线 / 口问题**：换一根确认能传数据的线（很多充电线只有 VBUS+GND，
   没有 D+/D−）；换主机后置 USB 口；避免在 Hub 上测试。
3. **VBUS 没到设备**：本设计不接 VBUS 检测脚（PA9），tinyusb 默认强制 B-Valid
   直接连接，无需 VBUS 引脚。若你改去读 VBUS，需同步在 `usb_hw_init()` 开 PA9 输入。
4. **Windows 驱动**：CMSIS-DAP v1 是标准 HID，Windows 免驱。若显示“未知 USB 设备”，
   多为上面 1/2/3 的硬件问题，而非驱动。

### 9.4 确认被识别

插上后设备管理器应出现一个 HID 设备，其详细信息里有
`STM32H743 CMSIS-DAP v1`（OpenOCD/pyOCD 靠这个串匹配）。命令行自检：

```bash
openocd -f openocd/stm32f429_cmsisdap.cfg -c "init" -c "dap info" -c "exit"
# 应能看到 CMSIS-DAP v1 探针信息，并打印目标 IDCODE 0x2BA01477
```

