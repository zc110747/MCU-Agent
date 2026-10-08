# 013.stm32h743_virtual_uart

STM32H743ZIT6 上的**软件 UART**：只用 GPIO + Timer + DMA + EXTI 产生和解析 UART 波形，
不使用任何硬件 USART 外设。PA9 / PA10 仅作普通 GPIO。

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
| `sw_uart.c`         | 顶层 API、统计、FIFO（Phase 3+） |

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

## Phase 0 状态

| 项 | 状态 |
|---|---|
| 工具链 / 启动 / linker / HAL TIM | ✅ |
| GPIOA MODER：PA9=Input, PA10=Output(PushPull) | ✅ SWD 实测 |
| PA9 PUPDR = PullUp | ✅ |
| TIM2/TIM3 PSC=0 ARR=20832, CR1.CEN=0 | ✅ SWD 实测 |
| APB1LENR = 0x3（TIM2/TIM3 使能） | ✅ |
| `.dma_buf` @ 0x24000000 32B 对齐 | ✅ 构建期强制 |
| 9600 bit error -0.0016 % | ✅ SWD 实测 |
| LED 心跳 500 ms | ✅ |
| TX/RX 数据通路 | ⬜ Phase 1+ |

Flash 11 092 B / 2 MB (0.53 %)，RAM 6 616 B / 512 KB (1.26 %)。

当前 `sw_uart_write/read` 返回 0 —— FIFO 属 Phase 3，**尚未实现，不做任何假传输**。
