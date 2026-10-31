# STM32F429 USB 触摸板鼠标（TinyUSB HID + GT911 + LVGL 8.3）

> **480×800 竖屏** NT35510 电容屏做成一块**触摸板**：手指在触摸区滑动 = 移动 PC 光标，
> 触摸区下方的**三个玻璃按键 = 鼠标左 / 中 / 右键**。整机通过 **TinyUSB device**
> 枚举成一个标准 HID 鼠标，插上就用，不需要装驱动。
> **滚轮暂不实现**（按需求留到下一步）。
>
> - 中文字库仍然**一个字节都不进 Flash**，全部放在 microSD 卡上（`0:/SYSTEM/FONT`）。
> - 面板 spec 标称 800×480，但 NT35510 在 `DFT_SCAN_DIR = L2R_U2D` 下报告的
>   **活动 GRAM 窗口是 480×800**（见 `bsp_lcd.c` 的 `lcd_scan_dir()`）。
>   所有 UI 常量按 480×800 写；`app_ui_create()` 发现实际窗口不符会打印
>   `[UI  ] WARN layout is ...`。

| 继承自 | 复用什么 |
|---|---|
| `102.stm32f429_tinyusb_ui` | 屏幕（FMC 8080 NT35510）、触摸（GT911 + 软 I2C + T_PEN EXTI）、SDRAM、LVGL 显示端口、TinyUSB 库 |
| `104.stm32f429_music` | 按键映射、SDIO 轮询驱动、FatFs 挂载、SD 卡 GBK 字库方案 |

---

## 1. 硬件与引脚

| 功能 | 外设 | 引脚 | 备注 |
|------|------|------|------|
| 系统时钟 | HSE | 25 MHz 晶振 | PLL M=25 N=336 P=2 → 168 MHz；Q=7 → 48 MHz（SDIO 与 USB 共用） |
| 调试串口 / 命令口 | USART1 | PA9 (TX) / PA10 (RX) | 115200 8N1，AF7；本机实测接的是 **COM3 (CP210x)** |
| **USB 设备** | OTG FS | **PA11 (DM) / PA12 (DP)** | AF10，Full Speed；`VID 0x0483 / PID 0x5720` |
| 状态 LED | GPIO | PB1 = LED0 心跳，PB0 = LED1（错误指示） | 低电平点亮 |
| 电容触摸 | 软 I2C | T_SCK = PH6 (SCL)，T_MOSI = PI3 (SDA) | GT9147/GT911，位操作，~250 kHz |
| 触摸复位 | GPIO | T_CS = PI8 | 推挽输出 |
| 触摸中断 | EXTI line 7 | T_PEN = PH7 | 下降沿，NVIC 优先级 6 |
| LCD | FMC Bank1 NE1 | 16-bit 8080，`LCD_BASE = 0x60000000 \| 0x0007FFFE` | NT35510（ID=0x8000），**活动窗口 480×800 竖屏** |
| 背光 | GPIO | PB5 | 高电平点亮 |
| microSD | SDIO 4-bit | PC8..PC11 = D0..D3，PC12 = CK，PD2 = CMD | AF12，**读走 DMA2_Stream3**，卡时钟 21 MHz，FatFs 卷 `0:` |
| SDRAM | FMC Bank1 | W9825G6KH，32 MB | `0xC0000000`，FreeRTOS 堆 + LVGL 堆 |
| 板载按键 | GPIO | KEY_UP = PA0（高有效），KEY0 = PH3，KEY1 = PH2，KEY2 = PC13（低有效） | 仅 KEY0 保留：清触摸轨迹（**不再当鼠标键**） |

> ⚠️ USB 要用板上 **USB_SLAVE / OTG 口**（PA11/PA12），不是 UART 那个口。
> 两个口同时插着才能一边看日志一边当鼠标用。

---

## 2. 时钟与内存布局

```
HSE 25 MHz ──► PLL ──┬─ SYSCLK 168 MHz (AHB 168 / APB1 42 / APB2 84)
                     └─ PLLQ 48 MHz ──► USB OTG FS（必须 48 MHz ±0.25%）
SDIO 卡时钟 = PCLK2 84 MHz / (ClockDiv + 2) = 84 / 4 = 21 MHz（ClockDiv = 2）
HAL 时基 = TIM7 1 ms（SysTick 归 FreeRTOS）
```

| 区域 | 内容 |
|------|------|
| 内部 SRAM | `.data` + `.bss` + 主栈，共 28.6 KB / 192 KB |
| SDRAM `0xC0000000` | FreeRTOS heap_5 单区域 **512 KB**（`.freertos_heap`，NOLOAD） |
| SDRAM `0xC0100000` | LVGL 堆 **256 KB**（`LV_MEM_ADR`，在 FreeRTOS 堆之外） |
| SDRAM 其余 | LVGL 显示缓冲 480×60×2 B ≈ 57 KB、任务栈、触摸轨迹 |

---

## 3. 软件架构

### 3.1 初始化顺序

```
HAL_Init → SystemClock_Config(168 MHz / 48 MHz)
 → BSP_LED_Init / BSP_UART_Init      (裸机即可打印)
 → bsp_key_init()
 → bsp_sdram_init() + 自测           ★ 必须先于任何 FreeRTOS 对象
 → vPortDefineHeapRegions(SDRAM)
 → fs_diskio_init()                  FatFs 互斥量
 → gesture_init()
 → 创建 usb / touch / ui / led 任务 → vTaskStartScheduler()
```

任务（优先级从高到低）：

| 任务 | 优先级 | 职责 |
|------|--------|------|
| `usb_mouse_task` | idle+5 | **第一件事**是 `usb_mouse_init()`（见 §5.2 的坑），之后循环 `tud_task_ext(5 ms)` + 发 HID 报告 |
| `touch_task` | idle+4 | 等 T_PEN 信号量 → 轮询 GT911（10 ms）→ `gesture_feed()`，同时把首个触点发布给 LVGL |
| `ui_task` | idle+2 | LCD/LVGL 初始化 → 探卡 → 装字库 → 建界面 → 每 5 ms `lv_timer_handler()` + **采样触摸板→鼠标** + 串口命令 |
| `led_task` | idle+1 | LED0 心跳 ~500 ms |

### 3.2 输入链路

```
GT911 ──(软 I2C, 只在 touch_task 里)──► bsp_touch_scan()  每 5 ms
                                            │
                      ┌─────────────────────┴─────────────────────┐
                      ▼                                           ▼
               trackpad_feed()   ← 鼠标走这条                gesture_feed()
                      │                                     (手势状态机)
        ┌─────────────┴──────────────┐                            │
        ▼                            ▼                            ▼
  usb_mouse_move_ex()        视觉环 (dot + 64 点轨迹环)      事件队列(32)
        │                            │                            │
        ▼                            ▼(异步、可滞后)              ▼
  usb_mouse_task              app_ui_poll()                app_ui_poll()
  → tud_hid_report()          → trail_add / dot_show       → 计数与提示
  每 2 ms 泵一次
```

- 触摸总线**只有** `touch_task` 访问，LVGL 渲染路径与鼠标路径上都没有 I2C 等待。
- **鼠标位移在 `touch_task` 里算，不在 UI 里算**。这一条是"跟手"的关键：
  之前 `app_ui_poll()` 每轮读一次触摸再算 delta，而那个循环要跑 LVGL 的
  `lv_timer_handler()`（含整屏 flush），实测只有 84 Hz 且抖动 12–40 ms —— 光标
  自然跟不上手指。现在 delta 在采样点当场算出并交给 USB，UI 只负责画圆点和轨迹，
  画面晚几毫秒没人看得出来。

### 3.3 触摸板 → 鼠标

| 手指动作 | 行为 |
|---|---|
| 单指落在触摸区（456×380）内 | 显示青色圆点 + 蓝色轨迹，并发 HID 相对位移 |
| 单指滑动 | 每 5 ms 采样一次，增量交给 `usb_mouse_move_ex()` |
| 单指滑出触摸区（滑到按键上） | **继续移动光标**（触摸板的正常手感），圆点停在边界 |
| 单指抬起 | 圆点立即消失，轨迹保留 600 ms 后淡出 |
| 手指**一开始**就按在玻璃键上 | 不移动光标，交给 LVGL 处理按键 |
| 双指 / 三指 | 手势状态机照常识别，界面显示「滚轮未启用」；**不产生滚轮报告** |

加速度（`usb_mouse.c`，Q8 定点，无浮点）。阈值是**速度**（px / 10 ms），不是
"每 tick 位移" —— 采样率变过两次（84 Hz → 172 Hz），按 tick 判档会让同样的手速
掉出高速档：

| 速度（px / 10 ms） | 增益 | 用途 |
|---|---|---|
| < 5 | 1.25× | 精确点选 |
| 5–11 | 1.75× | 常规移动 |
| ≥ 12 | 2.50× | 快速甩动（456 px 宽的板子要跨 1920 px 桌面） |

累积器是 Q8（1/256 px），**亚像素余量跨报告保留**，慢速拖动不会因为取整而丢失位移；
单份报告最多 ±127 px，超出的部分留在累积器里下一份继续发。

#### 跟手优化：四个延迟源

| # | 原来的问题 | 实测 | 改法 |
|---|---|---|---|
| 1 | 鼠标 delta 在 UI 循环里算，被整屏 flush 阻塞 | UI **84 Hz**，抖动 12–40 ms | 移到 `touch_task`（`app/trackpad.c`），采样即发 |
| 2 | HID 端点 `bInterval = 8 ms` | 光标上限 **125 Hz** | 改 `1 ms`（全速鼠标的标准值） |
| 3 | 触摸轮询 `TOUCH_POLL_MS = 10` | 采样 ~91 Hz | 改 `5 ms`（scan 实测 793 µs，占 ~14 % CPU） |
| 4 | `bsp_delay_us()` 每次都清 `DWT->CYCCNT` | 破坏所有基于周期计数的耗时测量 | 只启动一次，不再清零 |

改完实测：UI 循环 **172 Hz**（不再影响光标）、触摸采样 ~172 Hz（受限芯片本身的
100 Hz 刷新率）、报告排队延迟 **1 ms**、端点忙 **0**。
用 `s` 命令可读：

```
[PERF] ui=172Hz touch_scan=2717 us=793 send=17181 ep_busy=0 lat=1ms clk=168MHz
[PAD ] feeds=2716 moves=2410 clicks=1 drop=0
```

`us` 是单次 `bsp_touch_scan()` 的平均耗时，`lat` 是从 `usb_mouse_move*()` 到报告
真正发出的平均等待，`ep_busy` 是因主机未取走而放弃的次数。

> 再往下压只能改 GT911 的刷新率寄存器（默认 10 ms）或把 I2C 半周期从 3 µs 调到
> 2 µs，收益都在 20 % 以内，且要动芯片配置表，暂不做。

#### 回归修复：`moves=0`，滑动时鼠标完全不动

**现象**：触摸区滑动光标不动，状态卡"位移 X / Y"恒为 0：

```
[PAD ] feeds=10203 moves=0 clicks=865 drop=0
[MOVE] x=0 y=0
```

`feeds` 一直在涨说明采样在跑，`clicks=865` 说明触点被判成"按下了 865 次"，
`moves=0` 说明每次触点只拿到**一帧**坐标，第二帧就被当成松手 —— delta 恒为 0。

**根因**：GT911 的 `0x814E` 状态寄存器，bit7 = 坐标缓冲已更新，低 4 位才是触点数。
`bsp_gt9147_read()` 只取了低 4 位，而采样周期 5 ms 快于芯片 ~10 ms 的报告周期，
落在两帧之间的那次读取得到 `0x00`。**空帧被当成了松手**，笔画被切成一片片单帧。
（`GT_BUFFER_READY` 宏早就定义了，但从没被用过。）

**修法**：三层都区分"空帧"与"真抬起"：

| 层 | 值 | 含义 |
|---|---|---|
| `bsp_gt9147_read()` | `GT9147_NO_FRAME` (-2) | bit7=0，无新帧，**不清状态寄存器**，不报 0 触点 |
| `bsp_touch_scan()` | `BSP_TOUCH_NO_FRAME` (-2) | 原样上传，`s_last_pressed` 保持不变 |
| `touch_task` | — | 空帧只 `vTaskDelay` 后继续等；距最后一帧 `TOUCH_STALL_MS = 120` 才算接触丢失 |

同时修掉两处连带问题：

- `TOUCH_MAX_POLLS` 800 → 3000：4 s 的接触上限太短，正常慢速拖拽会被
  `contact aborted` 打断并重新 rearm 中断；
- abort 分支补 `trackpad_feed(0,0,0)`，否则指针会在逻辑上一直保持按下。

修后（手指按住并滑动）：

```
[PERF] ui=170Hz touch_scan=1423 us=454 send=4563 ep_busy=0 lat=1ms clk=168MHz
[PAD ] feeds=543 moves=503 clicks=1 drop=0        <- 93 % 的采样产生位移
[MOVE] x=-403 y=60
[TOUCH] irq=25 ready=1 fingers=1 st=0x81 pos=253,241 press=1 frame=531 empty=1365 release=3
```

`st=0x81` 就是"bit7 + 1 个触点"，`empty` 是空帧计数（约 2/3，符合 5 ms 采样对
10 ms 报告周期）。空闲时 `st=0x00 press=0 fingers=0`，不会自己漂移。

#### 界面反馈：轨迹提交节流

轨迹线覆盖整个触摸区（456×380 ≈ 350 KB 像素），每次更新都会让 NT35510 重绘约
35 ms。原来每个轨迹点都调一次 `lv_line_set_points()`（一帧最多 24 次），手指
滑动时 UI 循环被压到 **13 Hz**，看起来像"界面不刷新"。

改为：点只进数组，每 `TRAIL_COMMIT_MS = 60 ms` 提交一次整条线；光标圆点仍是
每帧更新（26×26，代价可忽略）。触摸时不再整屏重绘，空闲态实测 **170 Hz**。


### 3.4 三个玻璃键（左 / 中 / 右）

| 屏幕按钮 | HID 报告位 | 说明 |
|---|---|---|
| 左键 `LEFT` | bit0（HID Button 1） | `LV_EVENT_PRESSED` 置位，`RELEASED` / `PRESS_LOST` 清位 |
| 中键 `MIDDLE` | bit2（HID Button 3） | 按住不放可以拖拽（不是"点一下"语义） |
| 右键 `RIGHT` | bit1（HID Button 2） | 同上 |

> HID 的**按钮编号**是 1=左、2=右、3=中，所以报告位序是 `bit0=左, bit1=右, bit2=中`，
> 与屏幕上"左/中/右"的排列不同。`usb_mouse.h` 里的宏已经按报告位定义好。
>
> 板载实体按键**不再映射鼠标**：KEY0 现在只清触摸轨迹，KEY1/KEY2/KEY_UP 留空。

### 3.5 串口命令通道（USART1）

一个字符一条命令，回显走同一个口，整条 HID 链路可以不碰面板地被主机脚本驱动：

| 命令 | 作用 | 命令 | 作用 |
|------|------|------|------|
| `l` | 点一下左键（150 ms 后自动松开） | `s` / `?` | 状态快照（USB / 位移 / SD / 字库 / 触摸 / 界面） |
| `m` | 点一下中键 | `h` | 命令帮助 |
| `r` | 点一下右键 | `c` | 清触摸轨迹 |
| `x` | 光标右移（10×4 px 的"手指"位移） | `f` | 字库自检：逐字验证 12/16/24 px 字形能否读到 |
| `y` | 光标下移 | `k` | 裸扇区读测试（SDIO 层，绕开 FatFs） |
| 其它字符 | `[CMD ] unknown '<c>' (h for help)` | | |

```
$ python tools/serial_verify.py --port COM3 --reset
help             ok   [CMD ] l/m/r=tap button  x/y=nudge pointer  c=clear  s=statu
usb-mounted      ok   [USB ] mounted=1 suspended=0 buttons=0x00 reports=4
status-font      ok   [FONT] src=0: mask=0x1F
click-left       ok   [CMD ] left click
move-right       ok   [CMD ] move right
cursor-right     ok   (1022, 1035) -> (1135, 1035)     <- 主机侧光标真的动了
cursor-down      ok   (1135, 1035) -> (1135, 1066)
PASS 17 / FAIL 0
```

`serial_verify.py` 同时验证两层：**固件有没有执行命令**（串口回显）+ **主机有没有真的收到
HID 输出**（`user32!GetCursorPos` 前后对比、`reports=` 计数）。

### 3.5.1 界面中文显示不全：SDIO 读失败后不会恢复

状态卡里的中文时不时缺字，看起来像"字库不全"。用 `f` 命令逐字验证后真相是：

```
[FONT] 16px U+89E6 GBK=B4A5 FAIL seek=0 read=1 off=313280 got=0/32 slot=2
[FONT] check done: 118 CJK chars, map-miss=0 glyph-miss=354
```

- `read=1` 就是 FatFs 的 `FR_DISK_ERR`，`got=0` 一个字节都没读回；
- **偏移没有越界**（313 280 < 766 080），所以不是索引算错，也不是字库被裁剪；
- `k` 命令（裸扇区读，绕开 FatFs）当场成功 → 卡和 SDIO 控制器都能读。

根因在 `bsp_sdio_read_blocks()`：一次数据块传输失败后，SDIO 的 **DPSM 停在数据状态**，
之后每一条命令都会失败，而原实现只是把错误返回，没有任何恢复动作。于是第一次抖动
之后，所有字形读取永久失败。

修法（读写对称）：

| 位置 | 改动 |
|---|---|
| `bsp_sdio_read_blocks()` / `write_blocks()` | 失败后 `HAL_SD_Abort()`（内部发 CMD12）复位数据通路，再重试，最多 2 次（`SDIO_RW_RETRIES`） |
| 同上 | 等卡回到 `HAL_SD_CARD_TRANSFER` 超时也先 Abort 再返回，避免把卡留在忙状态 |
| `bsp_lcd_text.c` | 字模读取保持单次：重试只在 SDIO 层做，两层叠加会把一次卡死放大成 9 次 1 s 超时 |

修后实测：

```
[FONT] 12px file=574560 B, full GBK=574560 B
[FONT] 16px file=766080 B, full GBK=766080 B
[FONT] 24px file=1723680 B, full GBK=1723680 B
[FONT] check done: 118 CJK chars, map-miss=0 glyph-miss=0
```

> 字库布局：`.FON` 是 126 个 lead byte（0x81..0xFE）× 190 个 trail 槽 = **23 940 个字形**，
> 索引 `(qh - 0x81) * 190 + ql'`。16 px 每字 32 B → 766 080 B，三个尺寸都能整除，
> 说明卡上的字库是完整的。

### 3.5.2 SDIO 改成 DMA 接收：AC6 版本一个块都读不出来

用 ARM Compiler 6 编译的固件，`f_mount` 永远 `rc=1`，而同一份源码用 GCC 编译却正常：

```
[SDIO] core=168000000 pclk2=84000000 sdio=21000000 div=2 dma=on
[SD  ] SDIO init OK (30436 MB, block=512)
[SDIO] read fail st=3 err=0x00000020 STA=0x00000600 ...   <- 0x20 = RX_OVERRUN
[SD  ] f_mount failed rc=1
```

把时钟从 21 MHz 降到 8.4 MHz 完全没有改善（两档都以同样的方式失败），所以**不是总线速率
问题**。RX_OVERRUN 的含义是"卡送来的数据 CPU 没及时搬走"：SDIO 的接收 FIFO 只有 **128 字节
（32 words）**，21 MHz 下大约每 1.5 µs 就要取走一个字，轮询循环里任何一次中断延迟都会溢出；
AC6 把循环编译得更"紧凑"，恰好踩在这条线上。GD 侧只是运气好。

结论：**把接收交给 DMA，让传输不再依赖循环时序**，同时把卡时钟恢复成原来的 21 MHz。

| 位置 | 改动 |
|---|---|
| `bsp_sdio.c` | `bsp_sdio_read_blocks()` 改走 `HAL_SD_ReadBlocks_DMA()`，完成由 `HAL_SD_RxCpltCallback()` / `HAL_SD_ErrorCallback()` 置标志（`HAL_GetTick()` 走 TIM7，不依赖调度器） |
| 同上 | **`Init.Mode = DMA_PFCTRL`（外设流控）**，见下面这条坑 |
| `stm32f4xx_it.c` | 新增 `SDIO_IRQHandler` → `HAL_SD_IRQHandler()`、`DMA2_Stream3_IRQHandler` → `HAL_DMA_IRQHandler()`，优先级 6 / 5 |
| `fs_diskio.c` | 对齐判断从 4 字节收紧到 **16 字节**（DMA 用 `MBURST_INC4`），bounce 缓冲 `aligned(32)` |
| `bsp_sdio.c` | 调用方给不出 16 字节对齐缓冲时，内部按扇区经 `s_rx_bounce` 转存 |

> ⚠️ **必须用 `DMA_PFCTRL`，不能用 `DMA_NORMAL`。**
> SDIO 只在 RX FIFO **半满**（RXFIFOHF）时才发出 DMA 请求，而 `DMA_NORMAL` 模式下传输长度由
> DMA 自己数。实测表现是 `ndtr=4`、`lisr` 只有半传输位、SDIO 侧已经收到 DATAEND：块尾那几个
> 字凑不满半满阈值，永远等不到请求，传输就停在那里。改成外设流控后，由 SDIO 自己结束传输。
> 配置按 ST 官方 SDIO 例程：FIFO 模式 + `DMA_FIFO_THRESHOLD_FULL` + `INC4` 突发。

修后两个工具链都正常（21 MHz、`dma=on`）：

```
[SDIO] core=168000000 pclk2=84000000 sdio=21000000 div=2 dma=on
[SD  ] SDIO init OK (30436 MB, block=512)
[SD  ] mounted 0:
[FONT] check done: 118 CJK chars, map-miss=0 glyph-miss=0
```

写路径仍用轮询 `HAL_SD_WriteBlocks()` —— 运行期只读，写只出现在维护工具里，没必要再引一条
DMA 通道。

### 3.6 向量表：为什么不靠"VTOR 默认是 0"

`sys_startup/gcc/startup_stm32f429xx.s` 的 `Reset_Handler` 开头显式写
`SCB->VTOR = 0x08000000`。

本工程没有定义 `USER_VECT_TAB_ADDRESS`，所以 `SystemInit()` 不会设 VTOR，代码一直隐含
依赖"复位后 VTOR == 0，而 0 别名到 0x08000000"。一旦内核是从 ST 系统存储器 bootloader
被拉起，VTOR 就是 `0x1FFF0000`，而 FreeRTOS 的 `prvPortStartFirstTask()` **是透过 VTOR
读初始 MSP 的**，于是 MSP 被设成 bootloader 的值，第一次 `svc` 就异常。

实测症状：`CFSR = 0x00018200`（PRECISERR | UNDEFINSTR），PC 停在
`prvPortStartFirstTask+30`，串口只能打到 `FreeRTOS heap: ...` 就再无任务日志。

---

## 4. USB HID 实现（`app/usb_mouse.c` + `app/usb_descriptors.c` + `bsp/bsp_usb_hw.c`）

### 4.1 报告格式（3 字节，无 Report ID）

| 字节 | 内容 |
|---|---|
| 0 | 按键位：bit0 = 左，bit1 = 右，bit2 = 中 |
| 1 | X，相对位移，int8，-127…+127 |
| 2 | Y，相对位移，int8，-127…+127 |

不用 Report ID 是为了让设备在 BIOS/老系统里也能当鼠标用。**没有滚轮字节**——需求里
滚轮留到下一步，加的时候只需在描述符尾部和 `mouse_send()` 里各加一个字节。
端点：`EP1 IN`，中断，16 B，`bInterval = 8`。

### 4.2 两个必须记住的坑

**① `tusb_init()` 不能在 `main()` 里调用。**
它内部会 `dcd_init() → dcd_int_enable()`，把 OTG_FS 向量打开。此时 USB 线如果已经插着，
中断会在 `vTaskStartScheduler()` 内部就触发，而 ISR 里走的是
`xQueueSendToBackFromISR()`；FreeRTOS 在那个时刻的 ISR 上下文校验还不成立，直接命中
`vPortValidateInterruptPriority()` 的 `configASSERT(ucCurrentPriority >= ucMaxSysCallPriority)`，
程序停在 `Error_Handler`，串口只打到最后一条普通日志。

> 现场取证（gdb 回溯）：
> `OTG_FS_IRQHandler → dcd_int_handler → dcd_event_bus_reset → queue_event →
> osal_queue_send(in_isr=true) → xQueueGenericSendFromISR → vPortValidateInterruptPriority → assert`

正确做法：**在 USB 任务的第一行**做 `usb_mouse_init()`，让中断只在调度器运行后产生
（本工程就是这么做的），并在 `tusb_init()` 之后再把 NVIC 优先级按
`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` 设一遍。

**② TinyUSB 0.21 的 `TUD_CONFIG_DESCRIPTOR` 参数顺序变了。**

```c
/* 0.21: config_num 在最前面 */
TUD_CONFIG_DESCRIPTOR(config_num, _itfcount, _stridx, _total_len, _attribute, _power_ma)
```

照旧版 `(itf_count, config_num, …)` 写，会把 `bNumInterfaces` 填成 **0**。设备照样能枚举
到 VID/PID，但 Windows 会说"**该设备无法启动。(代码 10)**"，日志里能看到读完
configuration descriptor 后**直接 Suspend，从不发 SET_CONFIGURATION**，也从不请求 HID
report descriptor。改对之后 `00 09 01 00 …`（SET_CONFIGURATION）立刻出现，
`[USB ] mounted by host` 就打印了。

### 4.3 其它配置要点

- `CFG_TUD_VBUS_DETECT_HW = 0`：板上没做 VBUS 分压，必须让 DWC2 强制 B-session valid，
  否则内核永远不认为"已插入"。
- `CFG_TUSB_DEBUG = 0`：调试期设 2 能打印完整枚举过程（`USBD Setup Received …`），
  但那些 `printf` 很吃栈，**USB 任务栈要给到 1024 words（4 KB）**，2 KB 会溢出。
- OTG_FS 中断优先级 = 5（`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`），
  比它高（数值更小）就会在 ISR 里断言。

---

## 5. SD 卡目录要求

```
0:/
├─ SYSTEM/
│  └─ FONT/
│     ├─ GBK12.FON        12×12，24 B/字
│     ├─ GBK16.FON        16×16，32 B/字
│     ├─ GBK24.FON        24×24，72 B/字
│     ├─ GBK32.FON        32×32，128 B/字
│     └─ UNIGBK.BIN       （预留，当前未使用）
└─ IMG/                   （本版本不再使用，壁纸功能已移除）
```

- 字库格式：**MSB 在前、按列扫描**，每列 `ceil(box/8)` 字节；索引
  `190 × (GBK首字节 − 0x81) + (GBK尾字节 − 0x40 或 0x41)`。
  ASCII 不走 SD 卡，用 Flash 里编译进去的点阵表。
- 卡上没有 `SYSTEM/FONT` 时中文标签是空白，但界面、鼠标、按键照常工作。
- `f_open()` 对单个字体文件**重试 4 次**（每次间隔 5 ms）：SDIO 读虽然走 DMA，但初始化
  阶段的枚举仍是纯命令轮询，`f_mount` 后第一次取文件时偶发 `FR_DISK_ERR`，重试即可，
  否则会静默少一种字号
  （实测 `mask` 从 `0x1F` 掉到 `0x0F`）。

---

## 6. 构建 / 烧录

### 6.1 一键脚本（GCC）

```bat
build.bat              :: Debug   -> build\
build.bat release      :: Release -> build-release\
build.bat all          :: 两个都编
build.bat clean        :: 删 build\ build-release\ build_mdk\
```

脚本自己 configure（`Release` 自动带 `-DENABLE_PRINT_LOG=OFF`）、编译、统计警告数，
末尾打印 FLASH/RAM 占比。**警告数不为 0 时返回退出码 4**（零警告硬约束，可直接当门禁用）。
尺寸只在真正链接的那次才变，所以增量构建会复用上次结果。

### 6.2 手工 CMake

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build          # 产物: build/stm32f429_touchpad.elf / .hex / .bin

# 关掉日志（Release，一条 PRINT_LOG 都不编译进去）
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_PRINT_LOG=OFF
cmake --build build-release

openocd -f openocd.cfg -c "program build/stm32f429_touchpad.elf verify reset exit"
```

> 纯 GCC 构建（arm-none-eabi-gcc 15.3.1 + CMake + Ninja），零警告是硬要求：
> `-Wall -Wextra`，第三方（LVGL / FatFs / **TinyUSB**）单独 `-w`。
>
> **本板复位要注意**：`reset run` 不起作用（NRST 没接），`reset halt` 之后内核可能落到
> ST 系统存储器 bootloader 里。可靠的重启方式是手工装 Flash 向量：
> `python tools/serial_capture.py --ports COM3 --seconds 14`（脚本内部就是这么做的）。

### 6.3 MDK-ARM / uVision（ARM Compiler 6）

```bat
build_mdk.bat              :: Debug   -> build_mdk\Debug\
build_mdk.bat release      :: Release -> build_mdk\Release\
build_mdk.bat all          :: 两个都编
build_mdk.bat gen          :: 只重新生成 .uvprojx（源文件增删后跑这个）
build_mdk.bat all gen      :: 先重新生成再双构
```

也可以直接双击 `stm32f429_touchpad.uvprojx` 用 IDE 打开。

| 文件 | 作用 |
|------|------|
| `stm32f429_touchpad.uvprojx` | 两个 target（Debug/Release）、8 个分组、263 个文件 |
| `stm32f429_touchpad.sct` | armlink 分散加载文件，逐字节对应 `STM32F429IGTx_FLASH.ld` |
| `tools/gen_keil_project.py` | 生成 uvprojx；文件列表规则与 `CMakeLists.txt` 同源 |
| `tools/mdk_size.py` | 从 `.map` 出 FLASH/RAM/SDRAM 占比（UV4 只在重链接时打印 `Program Size`） |

几点与 GCC 侧不同的处理，都是实测撞出来的：

- **FreeRTOS 用 GCC 端口的 `port.c`，不是 RVDS 的**。AC6 是 clang，`__asm void f()` 那种
  armcc 独占的嵌入式汇编它不认，而 GCC 端口的内联汇编它能编。
- **`<RvdsVP>` 必须是 `2`** 才是 "Single Precision"。填 1/3/4 都不会给 armclang 传 FPU，
  `port.c` 会卡在 `#error ... enable hardware floating point support`（它以 `__VFP_FP__`
  为判据，而这个宏只在 FPU 打开时才由编译器定义）。
- **`ucHeap` 的段名分两种写法**：AC6 只按段名判断 RW/ZI，得用 `.bss.freertos_heap`；
  写成自定义名会被当成 RW 数据，armlink 于是在启动时把 512 KB 零从 Flash 拷到
  `0xC0000000`——那时 FMC 还没初始化，直接 HardFault。GCC 侧仍用 `.freertos_heap`
  （`.ld` 里 NOLOAD）。`#pragma arm section` 不行，那是 AC5 扩展。
- **`hal_flash_ex.c` 两边都加**：`hal_flash.c` 引用它的 `FLASH_Erase_Sector()` /
  `FLASH_FlushCaches()`。GNU ld 会先把没用的 `HAL_FLASHEx_Erase()` 段丢掉再解析符号，
  armlink 解析在前，所以缺了这个文件就 `L6218E`。加进去后 `--gc-sections` 又把它全丢掉，
  两侧体积没变。
- **`printf` 重定向**：AC6 + MicroLIB 走 `fputc()`，newlib 走 `_write()`，
  在 `app/syscalls.c` 里用 `__ARMCC_VERSION` 分成两支，不会重复定义。

---

## 7. 资源占用（当前构建）

| 项目 | GCC Debug | GCC Release（无日志） |
|------|-----------|----------------------|
| FLASH | 246 772 B / 1 MB（**23.53 %**） | 274 720 B / 1 MB（**26.20 %**） |
| 内部 RAM（.data + .bss） | 29 744 B / 192 KB（**15.13 %**） | 29 664 B / 192 KB（**15.09 %**） |
| SDRAM | 512 KB（FreeRTOS 堆，NOLOAD）+ 256 KB（LVGL 堆，固定地址） | 同左 |
| FreeRTOS 堆内的运行期分配 | 显示缓冲 57 KB + 任务栈 ~15 KB + 轨迹缓冲 | 同左 |

MDK-ARM（armclang 6.16，MicroLIB）：

| 项目 | Debug（-O1） | Release（-O2） |
|------|-------------|----------------|
| FLASH | 371 744 B / 1 MB（**35.45 %**） | 246 400 B / 1 MB（**23.50 %**） |
| 内部 RAM | 29 072 B / 192 KB（**14.79 %**） | 28 944 B / 192 KB（**14.72 %**） |
| SDRAM | 524 288 B（`RW_SDRAM`，UNINIT） | 同左 |

> 四档都是零警告。MDK 的 Debug 比 GCC 的大 123 KB，主要是 `-O1` 与 `-Og` 在 LVGL 上的
> 代码生成差异；Release 那一档 AC6 反而比 GCC `-Os` 小 28 KB。
> 两档都是 `-Wall -Wextra` **零警告**。相比纯触摸板版本（229 KB）多出的 ~12 KB 就是
> TinyUSB device 栈 + HID 类 + DWC2 端口。中文字库仍然 0 字节进 Flash。

---

## 8. 实测串口日志（USART1 / COM3, 115200）

```
STM32F429 touchpad demo
SDRAM Init ...
SDRAM Init OK
FreeRTOS heap: 512 KB @0xC0000000 (SDRAM)
[USB ] OTG FS device ready (HID mouse, 3 buttons, no wheel)
[TOUCH] task started (T_SCK=PH6 T_MOSI=PI3 T_CS=PI8 T_PEN=PH7)
[LCD ] controller ID = 0x8000
[LCD ] active GRAM window 480x800 (panel spec 800x480)
[UI  ] LCD 480x800 ready
[UI  ] LVGL 8.3.11 up
[SD  ] SDIO init OK (30436 MB, block=512)
[SD  ] mounted 0:
[UI  ] GBK glyph files: 0: (mask=0x1F)
[TOUCH] product ID = "911" (addr 0x14) -> MATCH
[TOUCH] stored config: version=0x51 resolution=480x800
[TOUCH] EXTI cfg: EXTICR2=0x00007000 IMR=0x0080 FTSR=0x0080 prio=6
[TOUCH] ready: id=911 addr=0x14 cfg=0x51, touch 480x800 -> canvas 480x800, swap=0 invX=0 invY=0
[USB ] mounted by host
[UI  ] screen built (storage ready)
```

Windows 侧识别结果：

```
HID\VID_0483&PID_5720\...        Class = Mouse    Status = OK   (HID-compliant mouse)
USB\VID_0483&PID_5720\105000000001  Class = HIDClass  Status = OK
```

---

## 9. 与 102 / 104 的差异

| 项目 | 102 | 104 | 本工程 105 |
|------|-----|-----|-----------|
| USB | TinyUSB **Host**（U 盘） | TinyUSB **Device**（MSC） | **TinyUSB Device（HID 鼠标）** |
| 触摸 | 单点，喂 LVGL | 无触摸 | **多点 + 触摸板→鼠标 + 手势识别** |
| 输入端 | 触摸点按钮 | 实体键 | **触摸板滑动 + 三个玻璃键** |
| 存储 | U 盘 + SD，卷 `0:` / `1:` | SD，卷 `0:` | 仅 SD，卷 `0:`（字库） |
| 字库 | SD/U 盘 FON | SD FON | SD FON（同 104 方案） |

---

## 10. 目录

```
app/     main.c  usb_mouse.c  usb_descriptors.c  tusb_config.h
         trackpad.c  touch_task.c  gesture.c  app_ui.c  ui_task.c
         sd_card.c  fs_diskio.c  image_sd.c  log.c  sdram_heap.c
         stm32f4xx_it.c  stm32f4xx_hal_timebase_tim.c  FreeRTOSConfig.h
bsp/     bsp_usb_hw.c  bsp_lcd.c  bsp_lcd_text.c  lcd_ascii_font.c
         lv_font_gbk.c  lv_gbk_map.c  bsp_touch.c  bsp_gt9147.c  bsp_sw_i2c.c
         bsp_sdio.c  bsp_sdram.c  bsp_key.c  bsp_led.c  bsp_uart.c  bsp_delay.c
         lv_port_disp.c  lv_port_indev.c
tools/   img2bin.py  gbk_font_gen.py  serial_capture.py  serial_verify.py
```

> `third_party/tinyusb/` 只编译 device 侧：`tusb.c`、`common/tusb_fifo.c`、
> `device/usbd.c`、`class/hid/hid_device.c`、`portable/synopsys/dwc2/{dcd_dwc2,dwc2_common}.c`。
> host 栈、MSC/CDC/Audio 类和别的 MCU 端口都不参与编译。

---

## 11. 验证状态与待办

已实测通过（串口 + SWD + Windows 主机侧三方取证）：

| 项目 | 证据 |
|------|------|
| 构建 | Debug / Release 双构零警告 |
| 烧录 | `Verified OK` |
| USB 枚举 | Windows 识别为 `HID-compliant mouse`，Status=OK、Problem=None |
| HID 报告 | `mounted=1 suspended=0 reports=8`，端点日志 `Queue EP 81 with 3 bytes` |
| 鼠标移动 | `x` / `y` 后光标实测 `1022→1135`、`1035→1066` |
| 三个按键 | `GetAsyncKeyState` 分别捕获到左/中/右按下 |
| SD 卡 / 字库 | 30436 MB；`mask=0x1F`（UNIGBK+GBK12/16/24/32 全在） |
| 触摸 IC | GT911，addr=0x14，cfg=0x51，480×800；`irq` 持续增长 |
| 串口命令 | `PASS 17 / FAIL 0` |
| 跟手延迟 | UI 循环 172 Hz、扫描 793 µs、报告排队 1 ms、端点忙 0 |

仍需**手感确认**（SWD 与串口都替代不了）：

1. **跟手程度**：手指在触摸区慢速滑动，光标应该贴合手指、没有"追赶"的拖尾感。
   若仍觉得迟滞，先看 `s` 的 `[PAD ] moves=` 是否随手指增长 —— 不增长说明
   `trackpad` 没收到样本（触摸板区域没注册或被按键抢占）。
2. **触摸板方向盘**：手指向右滑，PC 光标应向右。若方向相反或轴互换，改
   `bsp/bsp_touch.h` 的 `TOUCH_SWAP_XY / TOUCH_INVERT_X / TOUCH_INVERT_Y`
   （当前日志是 `swap=0 invX=0 invY=0`）。
3. **灵敏度**：默认 1.25× / 1.75× / 2.5× 三段（按 px/10 ms 判档）。觉得慢就整体乘一个
   系数（`usb_mouse.c` 的 `GAIN_*_Q8`），Windows 侧"提高指针精确度"也会叠加加速。
4. **三个玻璃键**：按住左键拖动图标试试；手指滑出按钮应立即松开（`PRESS_LOST`）。
5. **手势区**：双指上下/左右滑应显示"滚轮未启用"，不产生滚轮报告（符合当前需求）。
6. **滚轮**（下一步）：在 `usb_descriptors.c` 描述符尾部加 HID Usage `0x38`（Wheel），
   报告加第 4 字节；`gesture` 的 `SCROLL` 事件已经带 ±格数，接上即可。
