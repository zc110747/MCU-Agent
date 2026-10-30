# 104.stm32f429_music — STM32F429 音乐播放器

正点原子阿波罗 STM32F429IGT6（实验45 音乐播放实验）的 FreeRTOS + LVGL 重构版。
从 SD 卡的 `0:/MUSIC` 目录读取 **WAV / MP3** 播放到 WM8978，屏幕显示曲名 / 进度条 / 时间 /
目录文件数与可播曲目数，四个按键完成播放控制。构建与调试统一走
**CMake + Ninja + arm-none-eabi-gcc + OpenOCD/ST-Link + GDB**。

---

## 1. 硬件与引脚

| 功能 | 引脚 | 说明 |
|---|---|---|
| LED0 / LED1 | PB1 / PB0 | 低电平点亮 |
| 调试串口 USART1 | PA9 (TX) / PA10 (RX) | 115200 8N1，AF7 |
| SAI1_A I2S | PE2 MCLK_A, PE3 SD_A, PE4 FS_A, PE5 SCK_A, PE6 SD_B | AF6，master TX |
| WM8978 控制 | PH4 (SCL) / PH5 (SDA) | 位操作 I2C，~250 kHz |
| SDIO microSD | PC8..PC11 D0..D3, PC12 CK, PD2 CMD | AF12，4-bit，轮询（无 DMA） |
| LCD NT35510 | FMC Bank1 NE1，16-bit 8080 | `LCD_BASE = 0x60000000 \| 0x0007FFFE`，`lcd_id = 0x8000` |
| SDRAM W9825G6KH | FMC Bank2 (SDNE0)，32 MB | `SDRAM_BASE = 0xC0000000` |
| KEY_UP | PA0 | 下拉输入，按下为高 → **播放 / 暂停** |
| KEY0 | PH3 | 上拉输入，按下为低 → **下一首** |
| KEY1 | PH2 | 上拉输入，按下为低 → **音量 / 5 档** |
| KEY2 | PC13 | 上拉输入，按下为低 → **上一首** |

## 2. 时钟设计

```
HSE 25 MHz
  └─ PLL (M=25, N=336, P=2, Q=7)
       ├─ SYSCLK = 168 MHz ─┬─ AHB  = 168 MHz  (SDRAM / FMC / DMA)
       │                    └─ APB1 = 42 MHz / APB2 = 84 MHz
       └─ PLLQ   = 48 MHz  ──> SDIO + USB（F429 的 48 MHz 只能由 PLLQ 提供）
PLLI2S (N/Q/DIVQ 按时长率查表) ──> SAI_CK ──> MCLK = 256 × Fs
```

**为什么是 168 MHz 而不是 180 MHz**：180 MHz 时 `PLLQ` 只能取 7.5/8 → 45/45 MHz，
不满足 SDIO 要求的 48 MHz；168 MHz 下 `VCO = 336 MHz`，`Q=7` 正好 48 MHz。

**双时基**（关键设计）：

| 时基 | 归属 | 配置 |
|---|---|---|
| SysTick | FreeRTOS 调度器 | `configTICK_RATE_HZ 1000` |
| TIM12 | HAL `HAL_IncTick()` | `TIM8_BRK_TIM12_IRQn`，**抢占优先级 0（最高）** |

TIM12 的中断里只有 `HAL_IncTick()`，不调用任何 `...FromISR()` 接口，所以把它放到
`configMAX_SYSCALL_INTERRUPT_PRIORITY`(=5) 之上是安全的，且能保证 `HAL_Delay()`
在临界区里也不会丢拍。实现见 `app/stm32f4xx_hal_timebase_tim.c`。

## 3. 内存布局

| 区域 | 用途 |
|---|---|
| 内部 SRAM 192 KB | 主栈、普通变量、DMA 描述符 |
| CCM 64 KB | （本工程未使用） |
| SDRAM `0xC0000000` + 0 | **FreeRTOS heap_5 单区域 512 KB**（`ucHeap`） |
| SDRAM `0xC0100000` + 0 | LVGL 堆（`LV_MEM_ADR`） |
| SDRAM 剩余 | LVGL 显示缓冲（800 × 60 × 2 B ≈ 96 KB）、任务栈 |

`vPortDefineHeapRegions()` 必须在 `bsp_sdram_init()` **之后**、任何
`xTaskCreate()/pvPortMalloc()` **之前**调用，否则 FreeRTOS 会拿到一段不存在的内存。

## 4. 初始化顺序（`app/main.c`）

```
HAL_Init()                      // 同时启动 TIM12 时基
system_clock_config()           // 168 MHz / PLLQ 48 MHz
bsp_led_init() -> bsp_uart_init()
bsp_sdram_init()                // 外部 SDRAM 必须先活
vPortDefineHeapRegions()        // 之后才能用 pvPortMalloc
fs_diskio_init()                // FatFs 互斥量
lcd_driver_init()               // FMC + NT35510 + 清屏 + 开背光
sd_card_init() -> lcd_driver_font_init("0:")   // 挂载并从 SD 读 GBK 字库
bsp_wm8978_init() + 音量 + DAC/输出使能
bsp_key_init()
app_ui_init() -> audio_player_start() -> app_ui_start()
__set_BASEPRI(0); __enable_irq(); vTaskStartScheduler()
```

`__set_BASEPRI(0)` 是必需的：FreeRTOS V11 在进入调度器前最后一次临界区会留下
BASEPRI，若不清零，TIM12 中断会被屏蔽，`HAL_GetTick()` 停走。

## 5. 数据通路

```
SD 卡 (SDIO 轮询) -> FatFs -> track_decoder ─┬─ wav_decoder : 16/24 bit -> 16-bit stereo
                                             └─ mp3_decoder : minimp3   -> 16-bit stereo
                                             -> audio_player
                                             ├─ DMA2_Stream3 双缓冲 (M0AR/M1AR, CIRC)
                                             └─ SAI1_A_DR -> PE2..PE5 -> WM8978 -> 耳机/喇叭
LVGL 8.3.11 (SDRAM 堆) -> lv_port_disp -> lcd_color_fill() -> FMC -> NT35510
```

- **解码层与播放层解耦**：`track_decoder` 对外只暴露「16-bit 立体声 PCM + Fs」，
  播放引擎不知道源文件的格式；加格式只动 `track_decoder.c` 的分派，不动 `audio_player.c`。
- **音频缓冲**：每半缓冲 4096 B，DMA 传输完成中断 `DMA2_Stream3_IRQHandler` →
  `bsp_sai_dma_irq_handler()` → 应用回调释放信号量；实际填充在任务上下文做（FatFs 不可重入）。
- **暂停**：不停止 SAI/时钟链，而是向空闲半缓冲写 0 —— 恢复时无爆音、无重新同步开销。
- **单声道处理**：SAI 配置为 stereo master（2 slot/帧），单声道源会被复制到左右两个 slot，
  否则每帧吃掉 2 个源采样 → **2 倍速**。WAV 与 MP3 共用这条规则。
- **播完切歌**：读到 EOF 后先 `vTaskDelay` 让最后半缓冲播完，再切下一首（循环列表）。

### 5.1 MP3 支持要点

| 项目 | 取值 | 原因 |
|---|---|---|
| 解码器 | minimp3（单头文件，CC0） | 无动态内存、无系统调用，DCT 表全静态 |
| 配置 | `MINIMP3_ONLY_MP3` + `MINIMP3_NO_SIMD` | 省 ~40 KB 表；Cortex-M4 的 NEON 路径不适用 |
| 编译选项 | 仅该文件 `-mfloat-abi=softfp -mfpu=fpv4-sp-d16` | 解码是 float 实现；工程其余仍是 `soft` ABI，FPU 由 `vPortEnableVFP()` 使能 |
| 输出 | 16-bit stereo LE | 与 WAV 路径、SAI slot 配置统一 |
| 内存 | `s_mp3` ≈ 26 KB（`.bss`，内部 SRAM） | 见下两条 |

两处必须知道的实现细节：

1. **本地补丁**：上游 `mp3dec_decode_frame()` 在**栈上**放 `mp3dec_scratch_t`（16 256 B）。
   在 3 KB 的播放任务栈上点击 MP3 立刻 HardFault（栈溢出）——这就是「一点 MP3 就死机」
   的根因。本工程把它改成 `static`（进 `.bss`），并把 `AUDIO_TASK_STACK` 提到 6 KB 留余量。
2. **自己分帧，不要把整窗喂给解码器**：若一次传入的缓冲尾部截断了一个帧，minimp3 会
   `memset(dec, 0, ...)` 清掉 Layer III 的 **bit reservoir**，后续依赖它的帧会静默失败，
   表现为「只解出 76% 时长」。`mp3_decoder.c` 因此自己解析帧头、每次只喂完整一帧
   （`mp3_need()` → `mp3_parse_header()` → `mp3dec_decode_frame()` → `mp3_consume()`）。
   修前 385 帧里 87 帧被判 junk，修后 385/385，时长与 ffmpeg 偏差 < 1%，波形相关性 0.999998。

每首 MP3 播完会打印解码开销，用来判断还剩多少实时余量：

```
[MP3 ] decoded 277 s in 41235 ms (14% of realtime)
```

## 6. 按键与界面

| 按键 | 动作 |
|---|---|
| KEY_UP (PA0) | 播放 / 暂停 |
| KEY0 (PH3) | 下一首 |
| KEY1 (PH2) | 音量：5 档循环，到 1 或 5 处**换向**（1→5 后再 5→1），不跳回 |
| KEY2 (PC13) | 上一首 |
| 曲目结束 | 自动下一首（列表循环） |

音量档位映射到 WM8978 的 6 bit 衰减字段（0 = −57 dB、57 = 0 dB、63 = +6 dB）：

| 档 | 1 | 2 | 3（默认） | 4 | 5 |
|---|---|---|---|---|---|
| 寄存器码 | 45 | 51 | 55 | 58 | 61 |
| 增益 | −12 dB | −6 dB | −2 dB | +1 dB | +4 dB |

耳机（R52/R53）与喇叭（R54/R55）同步调整；最高档不用 63，避免小板载喇叭削顶。
界面左下角显示 `VOL n/5`。

按键为 `bsp_key_scan()` 轮询 + 2 次连续采样一致才生效（按下降沿触发，长按不重复）。
界面为深色单屏：

```
MUSIC PLAYER
┌───────────────────────────────────────────────┐
│ 大海-张雨生.mp3                                │
│ [██████████-------------------------]         │
│ 00:41                                  04:37  │
│ PLAYING                      MP3 44100 Hz / 16 bit │
│ FILES 12   MUSIC 12   NOW 11/12               │
└───────────────────────────────────────────────┘
KEY_UP: play/pause   KEY0: next   KEY1: volume   KEY2: prev      VOL 3/5
```

- `FILES n` = 目录里的**普通文件总数**，`MUSIC m` = 其中**可播数目**（`.wav` + `.mp3`），
  两者不等就说明有文件扩展名不支持（`.APE/.flac/.txt` 等）或文件名损坏。
- `NOW i/n` = 当前曲目在播放列表中的位置。
- 曲名超长自动滚动；中文字形来自 SD 卡 `0:/SYSTEM/FONT/GBK{12,16,24,32}.FON`，
  ASCII 来自编译期字模表。

## 7. 构建

```bash
# 依赖：arm-none-eabi-gcc >= 13、CMake >= 3.20、Ninja、OpenOCD、ST-Link 驱动
cmake -S . -B build     -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

cmake -S . -B build_rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_rel
```

- `-Wall -Wextra`，Debug / Release 双构**零警告**。
- `-DENABLE_PRINT_LOG=OFF` 可整体关掉 `PRINT_LOG()`（串口日志）。
- 一键脚本 `build_oneclick.bat`（configure / build / clean / flash）。
- VSCode：`Ctrl+Shift+B` 构建，F5 选 `Debug (OpenOCD + ST-Link)`（`.vscode/launch.json`）。

当前占用（全量重编，双构零警告）：

```
Debug    FLASH: 225268 B / 1 MB (21.48%)   RAM: 78808 B / 192 KB (40.08%)
Release  FLASH: 254800 B / 1 MB (24.30%)   RAM: 78824 B / 192 KB (40.09%)
```

RAM 里 ~26 KB 是 MP3 解码器的静态状态（`s_mp3`：16 KB minimp3 scratch + 5 KB 输入窗 +
4.5 KB PCM 暂存），Debug 版另含 FreeRTOS `configASSERT` 与日志字符串。

### 7.1 MDK-ARM（Keil）

工程文件不手工维护，单一真值仍是 `CMakeLists.txt`：

```bash
python MDK-ARM/gen_uvision.py    # 重写 .uvprojx / .uvoptx / .sct / startup
```

脚本先做一致性校验：`GROUPS` 里列的文件必须存在，`app/`（含 `app/ui/`，若存在）与
`bsp/` 下每个 `.c` 都必须进工程（唯一例外是 `syscalls.c`，见下表），否则直接报错退出。
**增删源文件后必须重跑一次**，否则 Keil 侧会与 GCC 侧悄悄漂移。

构建（GUI 打开 `MDK-ARM/stm32f429_music.uvprojx`，或命令行）：

```bash
"C:\Keil_v5\UV4\UV4.exe" -b MDK-ARM/stm32f429_music.uvprojx -j0 -o build.log
```

当前占用（AC6 6.14、全量重编、零错误零警告，256 个文件 / 27 个分组）：

```
Code=159832  RO-data=63868  RW-data=352  ZI-data=601296
FLASH 223936 B / 1 MB (21.4%)   RAM 77360 B / 192 KB (39.3%)   堆 512 KB 在 SDRAM
```

与 GCC 构建的差异（每条在脚本里都有注释）：

| 项 | GCC | MDK-ARM |
|---|---|---|
| 启动文件 | `sys_startup/gcc/startup_stm32f429xx.s` | 脚本复制 `sys_startup/arm/…`，**不改向量表** |
| 向量表指向 | `SVC_Handler` / `PendSV_Handler` / `SysTick_Handler` | 同左：`FreeRTOSConfig.h` 把 `vPortSVCHandler` 宏映射成 `SVC_Handler`，`port.o` 的强定义覆盖启动文件的 `[WEAK]` 桩 |
| 标准库 | newlib-nano + `app/syscalls.c` | MicroLIB + `MDK-ARM/mdk_target.c`（禁 semihosting、`fputc`→`uart_write`），`syscalls.c` 不入工程 |
| FLASH 驱动 | 只需 `hal_flash.c`（`-ffunction-sections --gc-sections` 丢掉未用函数） | 必须补 `hal_flash_ex.c`，否则 `FLASH_Erase_Sector` / `FLASH_FlushCaches` 未定义 |
| 分散加载 | `sys_startup/STM32F429IGTx_FLASH.ld` | `MDK-ARM/stm32f429_music.sct`（脚本生成，`umfTarg=0` 才不会被 uVision 自动生成的覆盖） |

`app/sdram_heap.c` 的堆段名按工具链分叉（`.freertos_heap` / `.bss.freertos_heap`），
两侧链接脚本同时兼容两个名字：

- **armclang** 只有看到 `.bss` 前缀才把段标成 ZI。否则生成 512 KB 的 PROGBITS 段，
  armlink 会给它一个 `LR_IROM1` 内的加载地址，`__main` 在 FMC 初始化之前就把它拷到
  `0xC0000000` → HardFault（`UNINIT` 只跳过 ZI 清零，拦不住 PROGBITS 拷贝）。
- **GCC** 反而不能用 `.bss` 前缀：链接脚本 `.bss` 输出段的 `*(.bss*)` 会抢先把它收走，
  实测 `region RAM overflowed by 406504 bytes`。

## 8. 烧录与调试

```bash
# 命令行烧录（务必用 .elf，OpenOCD 会自己算校验）
openocd -f openocd.cfg -c "program build/stm32f429_music.elf verify" -c "reset halt" -c "shutdown"
```

**不要**用 `-c "init; reset run; shutdown"` 一次性跑完：实测这样之后 PC 可能停在
`0x1FFFxxxx`（ROM 引导区）、`USART1_CR1 = 0`，应用根本没启动。可靠做法是先
`program ... verify` + `reset halt`，再单独 `resume`。

串口：`COM12`，115200 8N1。正常启动日志：

```
=== STM32F429 music player ===
[SDRAM] init OK
[LCD ] controller ID = 0x8000
[LCD ] active GRAM window 800x480 (dir 1, MADCTL 0xA0)
[SD  ] SDIO init OK (30436 MB, block=512)
[SD  ] mounted 0:
[FONT] GBK glyph files loaded from SD
[CODEC] WM8978 configured (volume 3/5)
[UI  ] LVGL 8.3.11 screen up (800x480)
[PLAY] 12/12 file(s) playable in 0:/MUSIC
[WAV ] 22050 Hz 1 ch 16 bit, 37 s
[SAI ] 22050 Hz / 16-bit ready (2048 items per half)
[PLAY] now playing: 0001.A_New_Hobby.wav
...
[MP3 ] 44100 Hz 2 ch 320 kbps, 277 s
[SAI ] 44100 Hz / 16-bit ready (2048 items per half)
[PLAY] now playing: 大海-张雨生.mp3
```

正常播放期间串口是**静默**的（不逐帧打印），只在切歌 / 按键 / 出错时输出。

## 9. SD 卡目录要求

```
0:/MUSIC/*.wav              // PCM 16/24 bit，单/双声道，任意常用采样率
0:/MUSIC/*.mp3              // MPEG-1/2/2.5 Layer III，CBR/VBR 均可，ID3v2 自动跳过
0:/SYSTEM/FONT/GBK12.FON    // 可选：没有则中文曲名不显示
0:/SYSTEM/FONT/GBK16.FON
0:/SYSTEM/FONT/GBK24.FON
0:/SYSTEM/FONT/GBK32.FON
```

扩展名大小写不敏感。播放列表在启动时扫描一次（最多 64 首，按名字排序）；换卡后需复位。
UI 上的 `FILES / MUSIC` 就是「扫描到的文件数 / 其中可播数」。

## 10. 目录结构

```
app/     main.c  system clock  FreeRTOSConfig.h  UI 播放引擎  track_decoder  WAV/MP3 解析  播放列表  FatFs 胶水
bsp/     SDRAM  SDIO  LCD(FMC)  LCD 文字/字库  SAI  WM8978  IIC  KEY  LED  UART  delay
third_party/  FreeRTOS V11.1.0  LVGL 8.3.11  FatFs  minimp3  CMSIS  HAL
cmake/   arm-none-eabi-gcc.cmake（工具链文件）
sys_startup/  GCC / ARM(Keil) / IAR 三套启动文件 + 链接脚本
MDK-ARM/ gen_uvision.py（生成 Keil 工程） mdk_target.c（MicroLIB 胶水） .uvprojx/.sct
```

## 11. 踩过的坑（值得记住）

1. **NT35510 是 480×800 面板**。横屏必须 `MADCTL(0x3600) = 0xA0`（MV=1 转置）
   且窗口设 800×480；若沿用 `MADCTL = 0x00` 又设 800 宽窗口，X 地址超过 479 会回卷，
   表现为「图形能显示但整体错乱」。`lcd_display_dir()` / `lcd_scan_dir()` 里按
   `dir` 做的镜像表 + bit5 交换宽高就是为此。
2. **单声道 WAV × stereo SAI = 2 倍速**。采样率对不代表速度对，帧率才是推流速率。
3. **`FF_LFN_UNICODE` 决定 `TCHAR` 宽度**。设 `1`（UTF-16）会让所有 `f_open()`
   参数变成 `WCHAR*` 而编译报指针类型不兼容；与 LVGL 的 UTF-8 对齐要用 `2`。
4. **FreeRTOS heap_5 + 外部 SDRAM** 的顺序是硬约束（见第 3 节）。
5. **`file(GLOB)` 收源文件时新增 `.c` 必须重新 `cmake -S -B` 配置**，否则链接报未定义。
6. **minimp3 的 16 KB scratch 在栈上**（上游实现），3 KB 播放栈一点 MP3 就栈溢出 HardFault；
   本工程改成 `static` 并把播放任务栈提到 6 KB。
7. **MP3 必须自己分帧**：整窗喂进 `mp3dec_decode_frame()` 时尾部截断会让它清空
   bit reservoir，后续帧静默丢失（表现为只播出约 76% 时长）。
8. **float 解码器要单独开 softfp**：`-mfloat-abi=softfp -mfpu=fpv4-sp-d16` 只加在
   `minimp3.c` 上；全工程改用 softfp 会牵动所有库的 ABI，FPU 也要靠 `vPortEnableVFP()` 打开。
9. **SDIO 传输被调试器打断会粘住 `FR_DISK_ERR`**：用 GDB 在播放中 `halt` 半途会
   让 FatFs 之后每次 `f_open()` 都返回 `rc=1`，整卷不可用直到重新挂载。
   `audio_task` 现在连续 3 首读失败就 `playlist_clear()` + `sd_card_invalidate()` 重挂载，
   不会再无限刷日志假装"死机"。
