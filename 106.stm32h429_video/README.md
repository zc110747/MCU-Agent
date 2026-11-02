# STM32F429 MJPEG 视频播放器（FreeRTOS + LVGL 8.3）

> 把 ALIENTEK 标准例程《实验47 视频播放器实验》搬到本仓库的 `app/` + `bsp/` 骨架上：
> **SD 卡上的 MJPEG/AVI 文件 → libjpeg 解压 → SDRAM surface → LVGL 全屏 image**，
> 同步声音走 **SAI1_A + DMA 双缓冲 → WM8978**，全程 **FreeRTOS 三任务**。
>
> - 与例程最大的不同：**libjpeg 不再直写 LCD GRAM / 不碰 DMA2D**，改成 RGB565 行钩子，
>   解码结果交给 LVGL 合成，叠加文字层不需要自己写混合。
> - 中文字库仍然**一个字节都不进 Flash**，全部放在 microSD 卡上（`0:/SYSTEM/FONT`）。
> - **当前状态：编译级验收完成，无硬件未上板。** 未验证项列在第 8 节。

| 继承自 | 复用什么 |
|---|---|
| `104.stm32f429_music` | SAI + WM8978 音频链（含 PLLI2S 采样率表）、软 I2C、SDIO 轮询驱动、FatFs 挂载、SD 卡 GBK 字库、按键映射 |
| `105.stm32f429_touchpad` | LCD（FMC 8080 NT35510）、SDRAM、LVGL 显示端口、UART 控制台、日志 |
| 例程《实验47》 | AVI 解包结构、libjpeg 解压子集、MJPEG 逐帧播放逻辑 |

深挖级的说明（为什么这样选、踩过什么坑、真机取证数字）**不在代码注释里**，见
**`document/stm32f429/video_player_port.md`**（代码里的 `@note` 会指到对应小节）。

---

## 1. 硬件与引脚

| 功能 | 外设 | 引脚 | 备注 |
|------|------|------|------|
| 系统时钟 | HSE 25 MHz | — | PLL M=25 N=336 P=2 → 168 MHz；Q=7 → 48 MHz（SDIO 用） |
| 调试串口 | USART1 | PA9 (TX) / PA10 (RX) | 115200 8N1，AF7，接板上 CH340 |
| 状态 LED | GPIO | LED0 = PB1，LED1 = PB0 | 低电平点亮 |
| 板载按键 | GPIO | KEY_UP = PA0（高有效），KEY0 = PH3，KEY1 = PH2，KEY2 = PC13（低有效） | 见第 5 节 |
| LCD | FMC Bank1 NE1 | 16-bit 8080，`LCD_BASE = 0x60000000 \| 0x0007FFFE` | NT35510，**800×480**（分辨率由探测填入，不写死） |
| microSD | SDIO 4-bit | PC8..PC11 = D0..D3，PC12 = CK，PD2 = CMD | AF12，**读走 DMA2_Stream6** |
| 音频数据 | SAI1_A | PE2 = MCLK_A，PE3 = SD_A，PE4 = FS_A，PE5 = SCK_A | AF6，I2S 主发；**DMA2_Stream3** |
| 音频控制 | 软 I2C | PH4 = SCL，PH5 = SDA | WM8978，位翻转实现 |
| SDRAM | FMC Bank1 | W9825G6KH-6，32 MB | `0xC0000000` |

> ⚠️ **DMA2 Stream 的分配是硬约束**：SAI 占 Stream3，SDIO 读占 Stream6。
> 105 的旧 `bsp_sdio.c` 用的是 Stream3，直接拷过来会和音频抢同一个 Stream。

---

## 2. 时钟与内存布局

```
HSE 25 MHz ──► PLL ──┬─ SYSCLK 168 MHz (AHB 168 / APB1 42 / APB2 84)
                     └─ PLLQ 48 MHz ──► SDIO
HAL 时基 = TIM12 (TIM8_BRK_TIM12_IRQn) 1 ms，抢占优先级 0
SysTick  →  FreeRTOS 内核节拍
```

| 区域 | 内容 |
|------|------|
| 内部 SRAM | `.data` + `.bss` + 主栈，约 23 KB / 192 KB |
| SDRAM `0xC0000000` | FreeRTOS heap_5 单区域 **3 MB**（`.freertos_heap`，NOLOAD） |
| SDRAM `0xC0300000` | LVGL 堆 **256 KB**（`LV_MEM_ADR`，在 FreeRTOS 堆之后） |
| SDRAM 其余 | 视频 surface 800×480×2 B = 750 KB、LVGL 绘制缓冲 800×60×2 B ≈ 94 KB、任务栈 |

时基为什么是 TIM12：SysTick 归内核后 `HAL_Delay()` 就没了时基，而 HAL 的 SDIO/SAI
初始化、`HAL_UART_Transmit` 超时都还在用它。详见文档 A1。

---

## 3. 代码结构

```
app/
  main.c                    时钟 / 外设 / 建任务 / 启动调度器
  video_player.c            播放列表 + 解包循环 + 帧节拍 + 按键语义
  video_avi.c               AVI(RIFF) 头解析
  video_mjpeg.c             libjpeg 驱动 + 38 KB bump pool + 4:4:4 扫描行
  mjpeg_row.h               libjpeg ↔ 工程的三个钩子
  video_ui.c                LVGL 视频屏（全屏 image + 上下文字带）
  audio_pcm.c               SAI DMA 双缓冲 → 半区信号量
  fs_diskio.c / sd_card.c   FatFs ↔ SDIO，卷 "0:" 挂载
  log.c / syscalls.c        日志与 newlib 重定向
  sdram_heap.c              heap_5 的 ucHeap（放 SDRAM）
bsp/
  bsp_lcd.c / bsp_lcd_text.c / lv_port_disp.c   FMC 8080 屏、GBK 字库、LVGL 显示端口
  bsp_sdio.c / bsp_sdram.c                      SDIO(DMA Stream6)、SDRAM
  bsp_sai.c / bsp_wm8978.c / bsp_iic.c          I2S 音频链
  bsp_uart.c / bsp_key.c / bsp_led.c            控制台、按键、LED
third_party/
  jpeg/                     libjpeg 解压子集（25 个 .c，jdmerge/jmemnobs 已改造）
  lvgl/ FreeRTOS-Kernel/ Fatfs/                 上游依赖（不入库）
```

---

## 4. 构建

**GCC（CMake + Ninja）**

```bash
cmake -S . -B build     -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
cmake -S . -B build_rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_rel -j
```

**MDK（ARMCLANG 6.14 + MicroLIB）**

```
MDK-ARM/stm32f429_video.uvprojx    两个 target：Debug / Release
UV4.exe -b MDK-ARM\stm32f429_video.uvprojx -t Release -j0 -o build_log.htm
```

`MDK-ARM/gen_uvision.py` 生成 uvprojx，源集必须与 `CMakeLists.txt` 逐项一致（改一处要同步另一处）。

当前构建结果（四份零警告）：

| 构建 | FLASH / Code+RO+RW | RAM (.data+.bss) | SDRAM |
|---|---|---|---|
| GCC Debug | 294 768 B (28.11%) | 23 712 B (12.06%) | 3 MB (9.38%) |
| GCC Release | 326 456 B (31.13%) | 23 744 B (12.08%) | 3 MB (9.38%) |
| MDK Debug | Code 376 006 / RO 60 682 / RW 280 | ZI 3 167 744 | — |
| MDK Release | Code 223 896 / RO 58 808 / RW 248 | ZI 3 167 776 | — |

---

## 5. 运行行为

### 支持的文件格式

只有一种组合，任一条件不满足都会在 `avi_init()` 阶段报错退出：

| 层 | 要求 | 判定位置 |
|---|---|---|
| 容器 | RIFF/AVI，顺序 `movi` chunk | `app/video_avi.c` |
| 视频 | `MJPG`（`biCompression` 与 `strh.fccHandler` 都要匹配） | `video_avi.c:193` / `:212` |
| 音频 | PCM（`wFormatTag == 1`），采样率在 `s_pll_presets[]` 内 | `video_avi.c` / `bsp/bsp_sai.c` |

**MP4 / H.264 不做**（已决策放弃，不要再往这个方向改）。原因是三层全要换：ISOBMFF
解复用要引入 `stbl` 五张表的随机访问，而 H.264 在 168 MHz Cortex-M4 上解 480p 差
一到两个数量级（无 cache、无硬解核，运动补偿要反复读参考帧，SDRAM 有效带宽吃不住）。
要播 MP4 请在 PC 端转码：

```bash
ffmpeg -i input.mp4 -c:v mjpeg -q:v 5 \
  -vf "scale=800:-2:force_original_aspect_ratio=decrease" \
  -c:a pcm_s16le -ar 44100 -ac 2 output.avi
```

MJPEG 每帧都是独立 JPEG，480p@15fps 约 2~4 MB/s，SD 卡至少要 Class 10。

### 启动与交互

- 上电顺序：`HAL_Init`(TIM12) → 168 MHz → LED/UART/KEY → **SDRAM** → heap_5 → SDIO 挂载 →
  WM8978 → 建任务 → `vTaskStartScheduler()`。SDRAM 必须先于任何 `pvPortMalloc`。
- 播放列表：`0:/VIDEO` 下所有 `.avi`，启动时一次收集（上限 32 个），播完循环。
- 按键（只在 `ui_task` 里 10 ms 轮询一次）：
  | 键 | 作用 |
  |---|---|
  | KEY0 | 下一个文件 |
  | KEY2 | 上一个文件 |
  | KEY_UP / KEY1 | 快进 / 快退 5 秒 |
- 帧节拍按 AVI 头里的 `dwMicroSecPerFrame` 走 `xTaskGetTickCount()`，
  解不过来就丢节拍继续，不积压。

---

## 6. 与例程的主要差异

| 方面 | 例程 | 本工程 |
|---|---|---|
| 解码输出 | libjpeg 直写 LCD GRAM + DMA2D | RGB565 **行钩子** → SDRAM surface → LVGL image |
| 音频 | 4 缓冲手工轮转 + TIM6 节拍 | SAI DMA 双缓冲 + 二进制信号量 + FreeRTOS tick |
| 文件切换 | `dir_sdi()` + `DIR.blk_ofs` 内部字段 | 启动时 `f_readdir()` 收集播放列表 |
| 时基 | SysTick / TIM6 | TIM12 给 HAL，SysTick 给内核 |
| 内存 | 内部 SRAM + 静态缓冲 | heap_5 3 MB + LVGL 256 KB，全在 SDRAM |
| 中文字库 | Flash 内点阵表 | SD 卡 `0:/SYSTEM/FONT`，LRU 缓存 |

---

## 7. 代码规范

`app/`、`bsp/` 已按 `mcu-code-style` 施工（`Drivers/`、`third_party/`、ST 配置文件不动）：

- 4 空格缩进、Allman 括号，`.clang-format` 一键对齐（clang-format 23.x）；
- 文件级状态合并为 `xxx_T` 结构体（`UART_STATE_T` / `SDIO_STATE_T` / `FONT_READER_T` …），
  `volatile` 落在成员上，非零默认值逐项显式写；
- 自研头文件一律 `extern "C"` 守卫；整型用 `uint8_t/int32_t`；
  无 `malloc/free`、无 VLA（动态内存统一 `pvPortMalloc`）；
- 注释只留摘要，坑与时序落 `document/stm32f429/video_player_port.md`。

---

## 8. 未验证项（无硬件）

目前只有**编译级验收**，以下都还没有真机结论：

- MJPEG 解码实际帧率、SDRAM surface 与 LVGL 刷新之间的撕裂；
- SAI/PLLI2S 各采样率的实际输出（预设表按公式算，未上示波器/听感验证）；
- SDIO 尾 FIFO 处理在本工程（Stream6）上的效果——相关取证来自 104/105；
- 按键 seek 边界、连续切文件时的句柄/内存泄漏；
- 播放列表上限（32）与长文件名的组合。
