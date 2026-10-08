# 013.stm32h743_virtual_uart

STM32H743ZIT6 上的**软件 UART**：只用 GPIO + Timer + DMA + EXTI 产生和解析 UART 波形，
不使用任何硬件 USART 外设。接线与 USART1 引脚一致——`PA9 = MCU TX → 主机 RX`、
`PA10 = MCU RX ← 主机 TX`，二者仅作普通 GPIO。

硬件：H743ZIT6 + 25 MHz 晶振 · arm-none-eabi-gcc 15.3.1 · CMake + Ninja · OpenOCD + ST-Link

---

## 目录

```
app/main.c              Phase 0 bring-up + SWD 探针
bsp/bsp.c|.h            时钟树 / LED / 板级
bsp/stm32h7xx_it.c      内核异常
bsp/sw_uart/            软件 UART 驱动（分层，见下）
sys_startup/            启动文件 + linker script
cmake/                  工具链 + .dma_buf 放置检查
```

### 驱动分层

| 模块 | 职责 |
|---|---|
| `sw_uart_config.h` | **唯一**硬件配置点：引脚 / 缓冲 / 波特率 / TIM / DMA |
| `sw_uart_gpio.c`    | BSRR 原子置位复位、GPIO 初始化 |
| `sw_uart_timer.c`   | bit timing 算术（PSC/ARR）、APB1 kernel clock 推导 |
| `sw_uart_dma.c`     | DMA stream 封装，memory→BSRR |
| `sw_uart_tx.c`      | 4096 B 环形 FIFO、10 word/字节编码、DMA 链 |
| `sw_uart_rx.c`      | EXTI 抓起始位 + TIM3 中点采样、1024 B FIFO |
| `sw_uart.c`         | 顶层 API、统计聚合 |

除 `sw_uart_config.h` 外，**没有任何文件直接写 GPIOA / GPIO_PIN_x / TIMx / DMA1_Stream0**。

---

## 时钟树与 bit timing

```
HSE 25 MHz →/2→ 12.5 MHz →×64→ 800 MHz VCO →/2→ SYSCLK 400 MHz
HCLK = 200 MHz          APB1/2/3/4 = 100 MHz
TIM2/TIM3 kernel = 200 MHz   ← APB1 分频≠1，故为 PCLK1 × 2
```

> ⚠️ `RCC->D2CFGR` 的 `D2PPRE1` 是**分频编码**而非分频值：`0`=不分频，
> `4`=/2，`5`=/4，`6`=/8，`7`=/16。只有 `0` 时 timer clock = PCLK1。

9600 bps 实测（SWD 读回，ARR=20832）：

| 项 | 值 |
|---|---|
| bit period | **104.165 µs**（理想 104.1667 µs） |
| 误差 | **-0.0016 %**（目标 < 1 %） |
| PSC / ARR | 0 / 20832（kernel 200 MHz 全速计数） |

各波特率理论误差均 ≤ 0.0064 %，PSC 固定为 0，量化误差仅 1 tick（5 ns）。

---

## DMA 内存放置

linker script 把 `.dma_buf` 段放在 **RAM_D1 0x24000000**：

- DMA1/DMA2 **无法访问 DTCM**（0x20000000），故 DMA 缓冲必须放 AXI SRAM
- 该段 32 字节对齐、尺寸为 32 的整数倍 → 可用 `SCB_Clean/InvalidateDCache_by_Addr`
  整行维护，不会误伤他人数据
- `cmake/check_dma_section.cmake` 在每次 build 后校验，落在 DTCM 或未对齐直接**构建失败**

---

## 构建

```bash
cmake --preset debug   && cmake --build --preset debug     # -Og -g3
cmake --preset release && cmake --build --preset release   # -Os
```

均需零警告。产物：`build/<cfg>/h743_virtual_uart.{elf,hex,bin,map}`

### 烧录 / 擦除 / 复位

```bash
cmake --build --preset debug --target flash    # 或 erase / reset
```

---

## TX 位周期自测（无需逻辑分析仪）

`tools/tx_selftest.c` 用 CPU 直接采样 TX 脚（PA9）的 IDR，并用 DWT 周期计数器给每个边沿打时间戳。
发送 `0x55`（每个 bit 边界都有边沿）时，**相邻边沿间隔就是 bit 周期**。

实测（400 次边沿，SWD 读回）：

| 项 | 实测 | 理想 | 误差 |
|---|---|---|---|
| min | 104.09 µs | 104.1667 µs | −0.074 % |
| max | 104.24 µs | 104.1667 µs | +0.070 % |
| avg | 104.16 µs | 104.1667 µs | −0.006 % |
| out_of_range | 0 / 460 | — | — |

需求 §41 目标 < 1 %，实测最大 0.07 %，**通过**。

两个必须注意的坑：

- **不能用 SysTick(1 kHz) 采样**：1 kHz 与 104.17 µs 之比恰为 9.6，采样点永远落在同样的
  10 个相位上，电平看起来不动（混叠），会误报"周期为 0"。必须主循环轮询。
- **自测图案必须每位都有边沿**：`0x00`/`0xFF` 会保持 9 个 bit 时间的同一电平，
  统计会被 9-bit 间隔污染。`0x55` 才是逐 bit 边沿的图案。

## Phase 0 状态

| 项 | 状态 |
|---|---|
| 工具链 / 启动 / linker / HAL TIM | ✅ |
| GPIOA MODER：PA9=Output(PushPull), PA10=Input(IT) | ✅ SWD 实测（Phase 3 修正接线） |
| PA10 PUPDR = PullUp | ✅ |
| TIM2/TIM3 PSC=0 ARR=20832, CR1.CEN=0 | ✅ SWD 实测 |
| APB1LENR = 0x3（TIM2/TIM3 使能） | ✅ |
| `.dma_buf` @ 0x24000000 32B 对齐 | ✅ 构建期强制 |
| 9600 bit error -0.0016 % | ✅ SWD 实测 |
| LED 心跳 500 ms | ✅ |
| TX/RX 数据通路 | ⬜ Phase 1+ |

Flash 11 092 B / 2 MB (0.53 %)，RAM 6 616 B / 512 KB (1.26 %)。

---

## Phase 3 状态 — TX FIFO 语义 + DMA 链

> ⚠️ **接线修正**：Phase 0 曾把软件 TX 配在 PA10、RX 配在 PA9，与硬件实际**相反**，
> 导致 PC 侧（ST-Link VCP，接收脚接 PA9）始终收到 0 字节，而 MCU 端 PA10 一直在翻转。
> 已改为 `PA9 = TX`、`PA10 = RX`。因 RX 引脚号 9 → 10，EXTI 向量同步
> `EXTI9_5 → EXTI15_10`。驱动是单文件配置，改动只落在 `sw_uart_config.h` 与 RX 的 EXTI 线。

`sw_uart_tx_write` 契约：**永不阻塞**。能放下的前缀被接受并返回长度，放不下的部分被
拒绝并计入 `tx_dropped`；**调用方必须按返回值推进偏移量**。

片上探针（SWD 读回 `g_phase3_probe`）：

| 用例 | 内容 | 实测 | 结论 |
|---|---|---|---|
| T1 | 空 FIFO 依次 write 1/16/256/4096/8 | 1/16/256/**3823**/0，`pending=4095`，`dropped=281`，`frames=1` | ✅ |
| T3 | 空 FIFO write 4097 | 接受 4095、拒 2 | ✅ |
| T5 | 连续灌两个整环 8192 B | `t5_written=8192` | ✅ |
| 守恒 | `tx_bytes == tx_frames` | 16383 == 16383 | ✅ 链无丢失 |
| 汇总 | `fail_count` | **0** | ✅ |

PC 侧（`tools/verify_phase3.py`，COM19 @9600，30 s）：

| 项 | 实测 | 门槛 |
|---|---|---|
| 收到字节 | 28 880 | ≥ 4000 |
| 最长 +1 run | 20 901 | ≥ 4096 |
| +1 比例 | **99.9861 %** | ≥ 99.9 % |

稳态 `0..255` 循环中除测试阶段 4 次图案重启外**零断链**（4 处跃变与 `w1→w16`、
`w16→w256`、`T1→T3`、`T3→T5` 边界逐一对应）。

**踩过的坑**：`app/main.c` 稳态循环最初写 `if (sw_uart_write(seq, 256) == 256) … else delay`，
**丢弃了部分写入的返回值**并从 `seq[0]` 重发。稳态下环满，每个 64 字节 DMA 块完成才腾出
64 B 空间，于是每轮只接受 `seq[0..63]`，在 `63 → 0` 处断链（每 64 字节一处 `delta=193`）。
驱动无错——`write` 的背压语义本就要求调用方用返回值推进偏移。

Flash 16 824 B / 2 MB (0.80 %)，RAM_D1 2 592 B / 512 KB (0.49 %)。
