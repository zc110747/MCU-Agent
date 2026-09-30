# Phase 1 报告：RT-Thread Nano + LED + UART

目标：在 RA8D1 Vision Board 上跑起 RT-Thread Nano 5.0.2，LED 呼吸闪烁，
串口（SCI9 @P208/P209，115200 8N1）作为 `rt_kprintf` 控制台并带 finsh/msh 交互。

结论：**全部达成**，验收脚本 15/15 通过，双构零警告。

---

## 1. 软件结构

```
entry()                       third_party/rt-thread-nano/src/components.c（__GNUC__ 提供）
  └─ rtthread_startup()
       ├─ rt_hw_board_init()            bsp/ra8d1-vision-board/bsp_rtthread.c
       │    ├─ rt_hw_systick_init()     SysTick = SystemCoreClock / 1000
       │    ├─ rt_system_heap_init()    64KB 静态数组（RT_USING_SMALL_MEM_AS_HEAP）
       │    ├─ bsp_uart_init()          FSP r_sci_b_uart（SCI9）
       │    └─ rt_components_board_init()
       ├─ rt_show_version()
       ├─ rt_application_init()  →  main 线程（RT_MAIN_THREAD_STACK_SIZE 2048）
       │     └─ rt_components_init() → INIT_APP_EXPORT(finsh_system_init) → tshell
       └─ rt_system_scheduler_start()
```

| 文件 | 职责 |
|---|---|
| `bsp/ra8d1-vision-board/bsp_rtthread.c` | 板级钩子：SysTick、堆、控制台输入输出 |
| `bsp/ra8d1-vision-board/bsp_uart.c` | SCI9 驱动：FSP open + RX 中断环形缓冲 + 轮询 TX |
| `bsp/ra8d1-vision-board/bsp_led.c` | P102 LED |
| `bsp/ra8d1-vision-board/cfg/rtconfig.h` | Nano 配置（**不开 `RT_USING_DEVICE`**） |
| `bsp/ra8d1-vision-board/gen/vector_data.c` | ICU slot 0 = `EVENT_SCI9_RXI` |
| `applications/main.c` | 心跳线程 + `led on/off/blink` msh 命令 |

`rtconfig.h` 刻意不打开 `RT_USING_DEVICE`：Nano 无设备框架时 `rt_kprintf` 直接走
`rt_hw_console_output()`，finsh 的 `finsh_getchar()` 走 `rt_hw_console_getchar()`，
链路最短，也不必实现 `rt_device` 的 open/read/write。

## 2. 中断与向量表

RA8D1 的 ICU 是「事件 → slot」两表结构，slot 序号 == NVIC IRQn：

```c
BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES] =
{ [0] = sci_b_uart_rxi_isr, };
const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
{ [0] = BSP_PRV_IELS_ENUM(EVENT_SCI9_RXI), };
```

## 3. 三个必须记住的坑

### 3.1 `IOPORT_CFG_NMOS_ENABLE` 会让 TXD 彻底哑掉

`0x00000040` 是 **N-Channel Open Drain**。从官方 BSP 抄引脚配置时把 P408/P409 的
NMOS 标志一起带到了 P208/P209，结果是 TX 脚永远拉不起来，串口 0 字节输出。

> 官方 `pin_data.c` 里 NMOS 只出现在 P408/P409（line 188/192），P208/P209（line 100/104）没有。

### 3.2 SCI_B 的 FIFO 模式下不能用 `TDRE` 做发送门控

现象：每次打印只出来前 16 个字符（`[heartbeat] tick` 之后全丢），
16 == `BSP_FEATURE_SCI_UART_FIFO_DEPTH`。

`SCI_B_UART_CFG_FIFO_SUPPORT = 1` 时驱动打开 FIFO，此时 `CSR_b.TDRE` 只反映
TDR↔移位寄存器的握手，**不反映 FIFO 是否已满**，于是轮询写会静默溢出丢字节。
驱动自己的 TXI ISR 用的是 FIFO 计数：

```c
uint32_t fifo_count = p_ctrl->p_reg->FTSR_b.T;      /* FTSR @0x54, T[5:0] */
for (cnt = fifo_count; (cnt < p_ctrl->fifo_depth) && ...; cnt++)
```

`bsp_uart_putc()` 改成 `while (fifo_depth <= FTSR_b.T) ;` 后立刻正常。

### 3.3 RT-Thread **不能**打成静态库（本次最隐蔽的一个）

现象：内核跑得好好的，心跳也在打印，但敲任何命令都没有回显，`help` 返回空。

根因链：

1. `INIT_APP_EXPORT(finsh_system_init)` 展开为一个放进 `.rti_fn.6` 段的
   `const init_fn_t` **变量**；
2. 链接器从 `.a` 抽取成员的唯一判据是「该成员定义了某个当前未定义的**符号**」；
3. `shell.c` 里没有别的被引用符号 → 整个成员不进链接 → `.rti_fn.6` 根本不存在。

证据（改前）：

```
02007ae0 T __rt_init_rti_start
02007ae4 T __rt_init_rti_board_start
02007ae8 T __rt_init_rti_board_end
02007aec T __rt_init_rti_end        ← 中间没有任何 INIT 项
```

`arm-none-eabi-nm build/firmware.elf | grep finsh_system_init` → 空。

修法：把 RT-Thread 源码直接编进 `firmware` executable（`-w` 关警告），
`--gc-sections` 照旧裁剪无用代码。改后：

```
02008748 T __rt_init_rti_start
0200874c T __rt_init_rti_board_start
02008750 T __rt_init_rti_board_end
02008754 T __rt_init_finsh_system_init   ← 回来了
02008758 T __rt_init_rti_end
```

> 通用判据：凡是用「链接器段表」注册自身的框架（RT-Thread 的 `.rti_fn` / `FSymTab`、
> 乃至 `__attribute__((constructor))`），都不要塞进静态库，或者必须 `--whole-archive`。

## 4. 资源占用（双构零警告）

| 构建 | text | data | bss | FLASH | RAM |
|---|---|---|---|---|---|
| Debug（交付产物） | 38916 | 212 | 91812 | **39128 B / 2MB = 1.87%** | **92024 B / 1MB = 8.77%** |
| Release | 34144 | 92 | 91844 | **34236 B / 2MB = 1.63%** | **91936 B / 1MB = 8.76%** |

bss 里最大的两块：RT-Thread 堆 64KB + `tshell` 栈 4KB + main 栈 2KB（后两个走堆）。

## 5. 验收

```bat
python tools/verify/verify_phase1.py
```

```
[PASS] boot banner              found 'RA8D1 Vision Board - Phase 1'
[PASS] heartbeat present        3 samples
[PASS] tick advances ~2000      ticks=[4016, 6019, 8022]
[PASS] msh prompt
[PASS] help lists 'led'         led registered
[PASS] cmd 'led on' / 'led off' / 'led blink' / 'led xx' / 'led'
[PASS] P102 high after 'on'     PODR=1,1
[PASS] P102 low after 'off'     PODR=0,0
[PASS] P102 toggles (blink)     3 transitions in 1.4 s
[PASS] ps threads               main/led/tshell/tidle0 alive
[PASS] free reports heap        total=65440 bytes
==============================================================
RESULT: 15 passed, 0 failed
```

LED 两项是**读硬件**判的，不看打印：脚本经 SWD 读 `P102` 的 `PmnPFS`（0x40400848，
bit0=PODR / bit1=PIDR），因此 `led on/off` 是否真的改变了引脚电平是可复核的。
