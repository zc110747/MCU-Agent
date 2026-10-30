# STM32F429 触摸板 / Touchpad demo（LVGL 8.3 + GT911 + microSD 字库/图片）

> **480×800 竖屏** NT35510 电容屏 + GT911 多点触摸，单指在画板上画线，双指/三指手势
> 被实时识别并显示；**中文字库和图片都不进 Flash，全部放在 microSD 卡上**。
> USB/OTG 本工程**不使用**（与 102 的区别）：没有 Host、没有 Device、也没有 U 盘。
>
> ⚠️ 面板 spec 标称 800×480，但 NT35510 在 `DFT_SCAN_DIR = L2R_U2D` 下报告的
> **活动 GRAM 窗口是 480×800**（见 `bsp_lcd.c` 的 `lcd_scan_dir()`，102 也是这个约定）。
> 所有 UI 常量按 480×800 写；`app_ui_create()` 若发现实际窗口不符会打印
> `[UI  ] WARN layout is ...`。

| 继承自 | 复用什么 |
|---|---|
| `102.stm32f429_tinyusb_ui` | 屏幕（FMC 8080 NT35510）、触摸（GT9147 + 软 I2C + T_PEN EXTI）、SDRAM、LVGL 显示端口、串口日志 |
| `104.stm32f429_music` | 按键映射（KEY0/KEY1/KEY2/KEY_UP）、SDIO 轮询驱动、FatFs 挂载、SD 卡 GBK 字库方案 |

---

## 1. 硬件与引脚

| 功能 | 外设 | 引脚 | 备注 |
|------|------|------|------|
| 系统时钟 | HSE | 25 MHz 晶振 | PLL M=25 N=336 P=2 → 168 MHz；Q=7 → 48 MHz（SDIO 专用） |
| 调试串口 / 命令口 | USART1 | PA9 (TX) / PA10 (RX) | 115200 8N1，AF7；接板载 CH340 或外置 USB-TTL |
| 状态 LED | GPIO | PB1 = LED0 心跳，PB0 = LED1（错误指示） | 低电平点亮 |
| 电容触摸 | 软 I2C | T_SCK = PH6 (SCL)，T_MOSI = PI3 (SDA) | GT9147/GT911，位操作，~250 kHz |
| 触摸复位 | GPIO | T_CS = PI8 | 推挽输出 |
| 触摸中断 | EXTI line 7 | T_PEN = PH7 | 下降沿，NVIC 优先级 6（≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY） |
| LCD | FMC Bank1 NE1 | 16-bit 8080，`LCD_BASE = 0x60000000 \| 0x0007FFFE` | NT35510（ID=0x8000），**活动窗口 480×800 竖屏** |
| 背光 | GPIO | PB5 | 高电平点亮 |
| microSD | SDIO 4-bit | PC8..PC11 = D0..D3，PC12 = CK，PD2 = CMD | AF12，**轮询（无 DMA）**，卡时钟 12 MHz，FatFs 卷 `0:` |
| SDRAM | FMC Bank1 | W9825G6KH，32 MB | `0xC0000000`，FreeRTOS 堆 + LVGL 堆 |
| 按键 | GPIO | KEY_UP = PA0（高有效），KEY0 = PH3，KEY1 = PH2，KEY2 = PC13（低有效） | 10 ms 轮询 + 2 次去抖 |

> SDIO 引脚与本板 FMC 不冲突：FMC 只用 PC0/PC2/PC3 与 PD0/PD1/PD8/PD9/PD10/PD14/PD15，
> PC8–PC12 与 PD2 空闲。

---

## 2. 时钟与内存布局

```
HSE 25 MHz ──► PLL ──┬─ SYSCLK 168 MHz (AHB 168 / APB1 42 / APB2 84)
                     └─ PLLQ 48 MHz ──► SDIO（卡时钟 48/4 = 12 MHz）
HAL 时基 = TIM7 1 ms（SysTick 归 FreeRTOS）
```

| 区域 | 内容 |
|------|------|
| 内部 SRAM | `.data` + `.bss` + 主栈，共 27.8 KB / 192 KB |
| SDRAM `0xC0000000` | FreeRTOS heap_5 单区域 **512 KB**（`.freertos_heap`，NOLOAD） |
| SDRAM `0xC0100000` | LVGL 堆 **256 KB**（`LV_MEM_ADR`，在 FreeRTOS 堆之外） |
| SDRAM 其余 | LVGL 显示缓冲 800×60×2 B ≈ 96 KB（`pvPortMalloc`）、笔画点缓冲 |

> 顺序是硬约束：`bsp_sdram_init()` → `vPortDefineHeapRegions()` → 任何 `xTaskCreate`。
> `.freertos_heap` 是 NOLOAD，启动代码不会在 FMC 就绪前访问 `0xC0000000`。

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
 → 创建 touch / ui / led 任务 → vTaskStartScheduler()
```

任务（优先级从高到低）：

| 任务 | 优先级 | 职责 |
|------|--------|------|
| `touch_task` | idle+4 | 等 T_PEN 信号量 → 轮询 GT9147（10 ms）→ `gesture_feed()`，同时把首个触点发布给 LVGL |
| `ui_task` | idle+2 | LCD/LVGL 初始化 → 探卡 → 装字库/图片 → 建界面 → 每 5 ms `lv_timer_handler()` + 抽手势事件 + 扫按键 |
| `led_task` | idle+1 | LED0 心跳 ~500 ms |

### 3.2 输入链路（谁碰硬件、谁碰 LVGL）

```
GT9147 ──(软 I2C, 只在 touch_task 里)──► bsp_touch_scan()
                                            │
                      ┌─────────────────────┴─────────────────────┐
                      ▼                                           ▼
            gesture_feed()  (状态机)                    bsp_touch_get()
                      │                                    (3 个 volatile 字)
                      ▼                                           ▼
              事件队列 (32 条)                             lv_port_indev
                      │                                           │
                      └────────► app_ui_poll() ◄──────────────────┘
                                 (ui_task, 唯一调用 LVGL 的地方)
```

- 触摸总线**只有** `touch_task` 访问，LVGL 渲染路径上没有任何 I2C 等待。
- 所有手柄/画线/清屏都在 `ui_task` 里做，避免跨任务调用 LVGL。

### 3.3 手势 / 事件

| 手势 | 事件 | 界面表现 |
|------|------|----------|
| 单指按下-移动-抬起 | `PRESS` / `MOVE` / `RELEASE` | 在画板上画一笔（圆头线，最多 256 点/笔、24 笔） |
| 单指快速点一下（<260 ms，位移 <14 px） | `TAP` | 盖一个圆点 |
| 双指竖滑 | `SCROLL`（每 24 px 一格） | 显示「双指下滑 ↓ / 上滑 ↑」并累计 |
| 双指横滑 | `PAN`（每 30 px 一格） | 显示「双指右滑 → / 左滑 ←」并累计 |
| 双指点击 | `TAP2` | 显示「双指单击」 |
| 三指点击 | `TAP3` | 显示「三指单击」 |

一次触摸里**一旦出现第二根手指**，当前这一笔立即收笔并锁进多指模式，直到全部抬起
——否则双指滚动会在画板上拉出一道斜线。

### 3.4 按键与屏幕按钮（两条通道，功能相同）

| 操作 | 按键 | 屏幕按钮 |
|------|------|----------|
| 清屏 | KEY0 (PH3) | 清屏 |
| 切换画笔颜色（6 色） | KEY1 (PH2) | 换色 |
| 切换线宽（2/4/8/12 px） | KEY2 (PC13) | 粗细 |
| 显示/隐藏壁纸 | KEY_UP (PA0) | 壁纸 |

> 画板之外的触摸不会落笔（按下点必须在画板矩形内），所以点按钮不会画出杂线。

### 3.5 串口命令通道（USART1）

屏幕上的四个按钮都有对应的串口命令，**一条命令一个字符**，回显走同一个口，
所以整块 UI 可以在不碰面板的情况下被主机脚本驱动：

| 命令 | 作用 | 命令 | 作用 |
|------|------|------|------|
| `c` | 清屏 | `s` / `?` | 状态快照（SD / 字库 / 触摸 / 手势 / 画笔 / 界面） |
| `k` | 换画笔颜色 | `h` | 命令帮助 |
| `w` | 换线宽 | 其它字符 | `[CMD ] unknown '<c>' (h for help)` |
| `b` | 显示/隐藏壁纸 | | |

```
$ python tools/serial_verify.py --port COM3 --reset
help             ok   [CMD ] c=clear k=colour w=width b=wallpaper s=status h=help
status-sd        ok   [SD  ] 0: mounted 136/30416 MB
status-font      ok   [SD  ] 0: mounted 136/30416 MB      <- 同一次 s 的四行之一
...
PASS 12 / FAIL 0
```

实现要点：

- 接收走 USART1 的 RXNE 中断 → 128 B 环形缓冲；**解析只在 `ui_task` 里做**，
  因为命令最终要调 LVGL，而 LVGL 只允许 `ui_task` 碰。
- 发送沿用两条路径：调度器未启动时阻塞轮询，启动后走 TXE 中断排空环形缓冲。
- 命令回显全部经 `PRINT_LOG()`，所以 `-DENABLE_PRINT_LOG=OFF` 的 Release 版
  仍然响应动作（清屏/换色…），只是没有文本回显。

---

## 3.6 向量表：为什么不靠"VTOR 默认是 0"

`sys_startup/gcc/startup_stm32f429xx.s` 的 `Reset_Handler` 开头会显式写
`SCB->VTOR = 0x08000000`。

本工程没有定义 `USER_VECT_TAB_ADDRESS`，所以 `SystemInit()` 不会设 VTOR，
代码一直隐含依赖"复位后 VTOR == 0，而 0 别名到 0x08000000"。一旦内核不是
从上电复位而是从 ST 系统存储器 bootloader 被拉起，VTOR 就是 `0x1FFF0000`，
而 FreeRTOS 的 `prvPortStartFirstTask()` **是透过 VTOR 读初始 MSP 的**，
于是 MSP 被设成 bootloader 的值，第一次 `svc` 就异常。

实测症状：`CFSR = 0x00018200`（PRECISERR | UNDEFINSTR），PC 停在
`prvPortStartFirstTask+30`，串口只能打到 `FreeRTOS heap: ...` 就再无任务日志。


---

## 4. SD 卡目录要求

```
0:/
├─ SYSTEM/
│  └─ FONT/
│     ├─ GBK12.FON        12×12，24 B/字   （可选，缺了 12 号中文不显示）
│     ├─ GBK16.FON        16×16，32 B/字
│     ├─ GBK24.FON        24×24，72 B/字
│     ├─ GBK32.FON        32×32，128 B/字
│     └─ UNIGBK.BIN       （预留，当前未使用）
└─ IMG/
   ├─ bg.bin              800×480 壁纸（可选，缺了用纯色背景）
   └─ <name>.bin          图标，尺寸由文件头决定（可选）
```

- 字库格式：**MSB 在前、按列扫描**，每列 `ceil(box/8)` 字节；索引
  `190 × (GBK首字节 − 0x81) + (GBK尾字节 − 0x40 或 0x41)`。
  ASCII 不走 SD 卡，用 Flash 里编译进去的点阵表。
- 图片格式：**LVGL v8 binary（.bin）**，`lv_img_header_t` 4 字节 + RGB565 像素。
  扩展名必须是**小写 `.bin`**（LVGL 内置解码器按字面比较扩展名；FAT 本身不区分大小写）。
- 卡上没有 `SYSTEM/FONT` 时中文标签是空白，但界面、按键、画板照常工作；
  卡完全没有时先显示 ASCII 启动页，10 s 后照样建界面，插卡后自动重建成完整界面。

### 4.1 生成这两个文件

```bash
# 图片 → LVGL .bin（需要有 Pillow）
python tools/img2bin.py wallpaper.png  sd/IMG/bg.bin --size 800x480
python tools/img2bin.py logo.png       sd/IMG/logo.bin

# TTF → GBKxx.FON（需要有 Pillow，四档一起生成）
python tools/gbk_font_gen.py C:/Windows/Fonts/simhei.ttf sd/SYSTEM/FONT
# 只生成 16 号，或只渲染部分首字节用于快速验证：
python tools/gbk_font_gen.py font.ttf out --sizes 16 --leads 0xB0 0xB0
```

字库体积参考：GBK12 ≈ 0.55 MB，GBK16 ≈ 0.73 MB，GBK24 ≈ 1.65 MB，GBK32 ≈ 2.9 MB。

---

## 5. 构建 / 烧录

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build          # 产物: build/stm32f429_touchpad.elf / .hex / .bin

# 关掉日志（省 Flash，一条 PRINT_LOG 都不编译进去）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_PRINT_LOG=OFF

openocd -f openocd.cfg -c "program build/stm32f429_touchpad.elf verify reset exit"
```

> 纯 GCC 构建（arm-none-eabi-gcc 15.3.1 + CMake + Ninja），零警告是硬要求：
> `-Wall -Wextra`，第三方（LVGL / FatFs）单独 `-w`。

---

## 6. 资源占用（当前构建，Debug）

| 项目 | Debug | Release（无日志） |
|------|-------|------------------|
| FLASH | 229 700 B / 1 MB（**21.91 %**） | 258 560 B / 1 MB（**24.66 %**） |
| 内部 RAM（.data + .bss） | 27 872 B / 192 KB（**14.18 %**） | 27 880 B / 192 KB（**14.18 %**） |
| SDRAM | 512 KB（FreeRTOS 堆，NOLOAD）+ 256 KB（LVGL 堆，固定地址） | 同左 |
| FreeRTOS 堆内的运行期分配 | 显示缓冲 480×60×2 B ≈ 57 KB + 任务栈 11 KB + 笔画点缓冲 ≤24 KB | 同左 |

> 两档都是 `-Wall -Wextra` **零警告**。中文字库 0 字节进 Flash：21.9 % 里主要是 LVGL 与 HAL。
> 注意 Release 反而比 Debug 大 29 KB：`-Os` 会把大量小函数内联展开，而 `-Og`
> 保持调用形式、配合 `--gc-sections` 掉得更多。要极限体积应换 `-Oz` 或关掉内联，本工程未做。

---

## 7. 实测串口日志（USART1 / COM3, 115200）

```
STM32F429 touchpad demo
SDRAM Init ...
SDRAM Init OK
FreeRTOS heap: 512 KB @0xC0000000 (SDRAM)
[TOUCH] task started (T_SCK=PH6 T_MOSI=PI3 T_CS=PI8 T_PEN=PH7)
[LCD ] controller ID = 0x8000
[LCD ] active GRAM window 480x800 (panel spec 800x480)
[UI  ] LCD 480x800 ready
[UI  ] LVGL 8.3.11 up
[SD  ] SDIO init OK (30436 MB, block=512)
[SD  ] mounted 0:
[UI  ] GBK glyph files: 0: (mask=0x1F)
[IMG ] 0:/IMG not found (rc=5) - wallpaper/icons disabled
[IMG ] wallpaper missing (solid colour)
[TOUCH] product ID = "911" (addr 0x14) -> MATCH
[TOUCH] stored config: version=0x51 resolution=480x800
[TOUCH] EXTI cfg: EXTICR2=0x00007000 IMR=0x0080 FTSR=0x0080 prio=6
[TOUCH] ready: id=911 addr=0x14 cfg=0x51, touch 480x800 -> canvas 480x800, swap=0 invX=0 invY=0
[UI  ] screen built (storage ready)
```

`[IMG ] 0:/IMG not found` 是因为当前卡上没建 `IMG/` 目录，壁纸走纯色背景，不影响其它功能。

命令通道实测（`tools/serial_verify.py --port COM3 --reset`）：

```
[SD  ] 0: mounted 136/30416 MB
[FONT] src=0: mask=0x1F
[TOUCH] irq=36 ready=1
[GEST] fingers=0 press=0 tap=0 multi=0 scroll=0 pan=0 drop=0
[BRUSH] color=1/6 width=4 px
[UI  ] screen=1 gesture_label=1
PASS 12 / FAIL 0
```

---

## 8. 与 102 / 104 的差异

| 项目 | 102 | 104 | 本工程 105 |
|------|-----|-----|-----------|
| USB | TinyUSB **Host**（U 盘） | TinyUSB **Device**（MSC） | **无**（相关源码已从工程移除） |
| 存储 | U 盘 + SD，卷 `0:` / `1:` | SD，卷 `0:` | 仅 SD，卷 `0:` |
| 字库 | SD/U 盘 FON | SD FON | SD FON（同 104 方案） |
| 图片 | 无 | 无 | **SD 上的 LVGL .bin**（壁纸/图标） |
| 触摸 | 单点，喂 LVGL | 无触摸 | **多点 + 手绘 + 手势识别** |

---

## 9. 目录

```
app/     main.c  touch_task.c  gesture.c  app_ui.c  ui_task.c
         sd_card.c  fs_diskio.c  image_sd.c  log.c  sdram_heap.c
         stm32f4xx_it.c  stm32f4xx_hal_timebase_tim.c  FreeRTOSConfig.h
bsp/     bsp_lcd.c  bsp_lcd_text.c  lcd_ascii_font.c  lv_font_gbk.c  lv_gbk_map.c
         bsp_touch.c  bsp_gt9147.c  bsp_sw_i2c.c  bsp_sdio.c  bsp_sdram.c
         bsp_key.c  bsp_led.c  bsp_uart.c  bsp_delay.c
         lv_port_disp.c  lv_port_indev.c
tools/   img2bin.py  gbk_font_gen.py  serial_capture.py  serial_verify.py
```

---

## 10. 验证状态与待办

已实测通过（SWD 读寄存器 + 串口命令回显双通道取证）：

| 项目 | 证据 |
|------|------|
| 构建 | Debug / Release 双构零警告 |
| 烧录 | `Verified OK` |
| SD 卡 | `HAL_SD_STATE_READY`，ErrorCode=0，30436 MB |
| FatFs | FAT32 / csize=32 / n_fatent=1946658 / free_clst=1937898 |
| 字库 | mask=`0x1F`（UNIGBK+GBK12/16/24/32 全在） |
| 触摸 IC | GT911，addr=0x14，cfg=0x51，480×800 |
| 触摸中断 | `EXTICR2=0x00007000`（Port H），irq 计数持续增长 |
| 屏幕分辨率 | `480x800`，与 UI 常量一致（无 `WARN layout`） |
| 串口 | USART1 / COM3 双向通，命令 12/12 PASS |

仍需人眼/手感确认（SWD 与串口都替代不了）：

1. **落点一致性**：手指点画板左上角，笔迹应落在左上角。若 X/Y 镜像或偏移，
   改 `bsp/bsp_touch.h` 的 `TOUCH_SWAP_XY / TOUCH_INVERT_X / TOUCH_INVERT_Y`。
   当前日志显示 `touch 480x800 -> canvas 480x800, swap=0 invX=0 invY=0`。
2. **画线流畅度**：单笔最多 256 点，每次移动整笔重绘。若明显拖影，把
   `app_ui.c` 的 `STROKE_MIN_STEP` 调大或缩短单笔点数。
3. **双指/三指**：`bsp_gt9147.c` 按 5 点读取，需在屏上确认 `[GEST]` 计数增长
   （可直接用串口 `s` 命令看 presses/taps/multi/scroll/pan）。
4. **壁纸**：卡上还没有 `IMG/` 目录，所以现在是纯色背景。放
   `python tools/img2bin.py wallpaper.png sd/IMG/bg.bin --size 480x800` 生成的
   `.bin` 进去，重启或插拔卡即可生效（整屏逐行读卡，换壁纸那一帧会稍慢，属预期）。
