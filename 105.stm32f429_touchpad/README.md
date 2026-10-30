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
| microSD | SDIO 4-bit | PC8..PC11 = D0..D3，PC12 = CK，PD2 = CMD | AF12，**轮询（无 DMA）**，卡时钟 12 MHz，FatFs 卷 `0:` |
| SDRAM | FMC Bank1 | W9825G6KH，32 MB | `0xC0000000`，FreeRTOS 堆 + LVGL 堆 |
| 板载按键 | GPIO | KEY_UP = PA0（高有效），KEY0 = PH3，KEY1 = PH2，KEY2 = PC13（低有效） | 仅 KEY0 保留：清触摸轨迹（**不再当鼠标键**） |

> ⚠️ USB 要用板上 **USB_SLAVE / OTG 口**（PA11/PA12），不是 UART 那个口。
> 两个口同时插着才能一边看日志一边当鼠标用。

---

## 2. 时钟与内存布局

```
HSE 25 MHz ──► PLL ──┬─ SYSCLK 168 MHz (AHB 168 / APB1 42 / APB2 84)
                     └─ PLLQ 48 MHz ──► SDIO（卡时钟 48/4 = 12 MHz）
                                        └─ USB OTG FS（必须 48 MHz ±0.25%）
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
GT911 ──(软 I2C, 只在 touch_task 里)──► bsp_touch_scan()
                                            │
                      ┌─────────────────────┴─────────────────────┐
                      ▼                                           ▼
            gesture_feed()  (状态机)                    bsp_touch_get()
                      │                                    (3 个 volatile 字)
                      ▼                                           ▼
              事件队列 (32 条)                          ┌─────────┴──────────┐
                      │                                 ▼                    ▼
                      │                          app_ui_poll()        lv_port_indev
                      │                          (触点→鼠标位移)      (命中三个玻璃键)
                      ▼                                 ▼                    ▼
                 app_ui_poll() ──► usb_mouse_move()                LV_EVENT_PRESSED
                                   usb_mouse_set_button()          → HID button bit
                                            │
                                            ▼
                                  usb_mouse_task → tud_hid_report()
```

- 触摸总线**只有** `touch_task` 访问，LVGL 渲染路径与鼠标路径上都没有 I2C 等待。
- 触摸板坐标用 **UI tick（5 ms）采样 `bsp_touch_get()`**，而不是用 gesture 的 `MOVE`
  事件：后者有 2 px 量化阈值，做鼠标会顿。手势事件仍保留，只用于计数与文字提示。

### 3.3 触摸板 → 鼠标

| 手指动作 | 行为 |
|---|---|
| 单指落在触摸区（456×380）内 | 显示青色圆点 + 蓝色轨迹，`usb_mouse_move(dx, dy)` |
| 单指滑动 | 逐 tick 取 `dpad`，经加速度曲线换成 HID 相对位移 |
| 单指抬起 | 圆点立即消失，轨迹保留 600 ms 后淡出 |
| 落在触摸区外（三个玻璃键上） | **不移动光标**，交给 LVGL 处理按键 |
| 双指 / 三指 | 手势状态机照常识别，界面显示「滚轮未启用」；**不产生滚轮报告** |

加速度（`usb_mouse.c`，Q8 定点，无浮点）：

| 单 tick 位移量 | 增益 | 用途 |
|---|---|---|
| 1–4 px | 1.25× | 精确点选 |
| 5–11 px | 1.75× | 常规移动 |
| ≥ 12 px | 2.50× | 快速甩动（480 px 宽的板子要跨 1920 px 桌面） |

累积器是 Q8（1/256 px），**亚像素余量跨报告保留**，慢速拖动不会因为取整而丢失位移；
单份报告最多 ±127 px，超出的部分留在累积器里下一份继续发。

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
| `r` | 点一下右键 | 其它字符 | `[CMD ] unknown '<c>' (h for help)` |
| `x` | 光标右移（10×4 px 的"手指"位移） | `c` | 清触摸轨迹 |
| `y` | 光标下移 | | |

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
- `f_open()` 对单个字体文件**重试 4 次**（每次间隔 5 ms）：SDIO 是轮询读，USB 中断如果
  正好落在数据块传输中间会让命令超时（`FR_DISK_ERR`），重试即可，否则会静默少一种字号
  （实测 `mask` 从 `0x1F` 掉到 `0x0F`）。

---

## 6. 构建 / 烧录

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

---

## 7. 资源占用（当前构建）

| 项目 | Debug | Release（无日志） |
|------|-------|------------------|
| FLASH | 240 268 B / 1 MB（**22.91 %**） | 269 656 B / 1 MB（**25.72 %**） |
| 内部 RAM（.data + .bss） | 28 592 B / 192 KB（**14.54 %**） | 28 600 B / 192 KB（**14.55 %**） |
| SDRAM | 512 KB（FreeRTOS 堆，NOLOAD）+ 256 KB（LVGL 堆，固定地址） | 同左 |
| FreeRTOS 堆内的运行期分配 | 显示缓冲 57 KB + 任务栈 ~15 KB + 轨迹缓冲 | 同左 |

> 两档都是 `-Wall -Wextra` **零警告**。相比纯触摸板版本（229 KB）多出的 ~11 KB 就是
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
         touch_task.c  gesture.c  app_ui.c  ui_task.c
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

仍需**手感确认**（SWD 与串口都替代不了）：

1. **触摸板方向盘**：手指向右滑，PC 光标应向右。若方向相反或轴互换，改
   `bsp/bsp_touch.h` 的 `TOUCH_SWAP_XY / TOUCH_INVERT_X / TOUCH_INVERT_Y`
   （当前日志是 `swap=0 invX=0 invY=0`）。
2. **灵敏度**：默认 1.25× / 1.75× / 2.5× 三段。觉得慢就整体乘一个系数
   （`usb_mouse.c` 的 `GAIN_*_Q8`），Windows 侧"提高指针精确度"也会叠加加速。
3. **三个玻璃键**：按住左键拖动图标试试；手指滑出按钮应立即松开（`PRESS_LOST`）。
4. **手势区**：双指上下/左右滑应显示"滚轮未启用"，不产生滚轮报告（符合当前需求）。
5. **滚轮**（下一步）：在 `usb_descriptors.c` 描述符尾部加 HID Usage `0x38`（Wheel），
   报告加第 4 字节；`gesture` 的 `SCROLL` 事件已经带 ±格数，接上即可。
