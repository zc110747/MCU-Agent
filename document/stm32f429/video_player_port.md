# STM32F429 视频播放器移植要点（106.stm32h429_video）

源例程：ALIENTEK 标准例程 HAL 库版《实验47 视频播放器实验》。
目标骨架：与 104/105 相同的 `app/` + `bsp/` + `third_party/` 结构，FreeRTOS V11 + LVGL 8.3。

代码里的注释只留一句话摘要，**坑、时序、推导、取证都在本文对应的 A 小节**。
下文数字均取自源码常量，未在真机验证的项单独列在第 A10 节。

---

## A1 运行形态与内存布局

| 项 | 取值 | 位置 |
|---|---|---|
| RTOS | FreeRTOS V11，ARM_CM4F port（GCC 侧 `port.c`，ARMCLANG 侧 `portable/RVDS`） | `third_party/FreeRTOS-Kernel` |
| 内核堆 | heap_5，`configTOTAL_HEAP_SIZE` = 3 MB，落在外部 SDRAM `0xC0000000` | `app/sdram_heap.c` |
| LVGL 内存 | `LV_MEM_ADR = 0xC0300000`，`LV_MEM_SIZE` = 256 KB | `third_party/lvgl/lv_conf.h` |
| SDRAM | W9825G6KH-6，32 MB，FMC Bank1，16-bit | `bsp/bsp_sdram.c` |
| HAL 时基 | TIM12（`TIM8_BRK_TIM12_IRQn`），1 ms，抢占优先级 **0** | `app/stm32f4xx_hal_timebase_tim.c` |
| 系统节拍 | SysTick 交给 FreeRTOS（`TICK_INT_PRIORITY 0`） | — |

为什么时基必须是 TIM12 而不是 SysTick：SysTick 一旦归 FreeRTOS，`HAL_Delay()` 就没有
时基了；而 `HAL_Delay()` 又是 HAL 内部（SDIO / SAI 初始化、`HAL_UART_Transmit` 超时）到处
在用的。用 TIM12 让两者各自独立，`HAL_IncTick()` 也不会在内核的 tick 处理里重入。
优先级给 0 是为了让 HAL 的超时计数不被 `BASEPRI` 屏蔽拖慢——但反过来说，FreeRTOS 的
临界区（`taskENTER_CRITICAL()` 抬 BASEPRI 到 5）挡不住它，所以 TIM12 的 ISR 里只做
`HAL_IncTick()`，绝不调用 FreeRTOS API。

为什么内核堆要放 SDRAM：内部 SRAM 只有 192 KB，而这个工程同时要 LVGL 对象、
video_player 任务栈（1536 words = 6 KB）、LVGL 的 256 KB 内存池（在堆区之后）、
以及 JPEG 解码的 38 KB bump pool。放在内部 SRAM 会立刻见底。

`main()` 在 `vPortDefineHeapRegions()` 之前不能创建任何 FreeRTOS 对象；SDRAM 初始化
（`bsp_sdram_init()`）必须排在它前面。启动顺序见 `app/main.c` 的注释块。

`__set_BASEPRI(0)` 放在 `vTaskStartScheduler()` 之前：判断优先级分组 / 清掉可能在
HAL 初始化阶段被抬起的 BASEPRI，否则第一个任务可能进不去。

---

## A2 显示链路

- 屏：8080 MCU 屏，FMC Bank1 NE1，NT35510，**800x480**。分辨率不写死，由
  `bsp_lcd.c` 探测后填 `g_lcd_info.lcd_width/lcd_height`，上层统一用 `LCD_Width/LCD_Height`。
- 视频帧 **不直写 GRAM**：解码结果先落一块 SDRAM surface（800x480x2 = 768 000 B），
  LVGL 用一个 `LV_IMG_CF_TRUE_COLOR` 的全屏 image 指向它，每帧
  `lv_obj_invalidate()` + `lv_refr_now()`。
  例程的做法是 libjpeg 直接喷 GRAM + DMA2D 搬运，那样一旦要叠 UI（标题、进度、
  文件名字带）就得自己做混合；走 LVGL 则文字层是免费的，代价是每帧多一次整屏刷新
  （`lcd_color_fill()` 一次性推 800 行）。
- LVGL 绘制缓冲：`DISP_BUF_LINES = 60` 行，800x60x2 = 96 000 B（注释里写的 57 kB 是按
  480 宽算的旧值），从 SDRAM `pvPortMalloc` 出来，不占内部 SRAM。
- `disp_flush()` 是阻塞推屏（`lcd_color_fill()` 里等 FMC 写完），立即
  `lv_disp_flush_ready()`；不需要双缓冲。

---

## A3 MJPEG 解码（libjpeg 解压子集）

`third_party/jpeg/` 是 libjpeg 的**解压子集**（25 个 `.c`，`CMakeLists.txt` 里显式列出）。
三处相对上游的改动，升级 libjpeg 时必须重放：

1. **`jdmerge.c`**：两个 upsampler 被改成 RGB565 行输出。
   每输出一行调用 `mjpeg_row_buffer()` 要缓冲、`mjpeg_row_emit(row, width)` 交给上层，
   不再 `#include "lcd.h"` 直写显存。宏 `MJPEG_PACK_RGB565()` 从 `range_limit[]` 取
   Y/Cb/Cr 并打包。
2. **`jmemnobs.c`**：`malloc/free` 换成 `mjpeg_pool_alloc()` / 无操作 free，
   池子是 `app/video_mjpeg.c` 里 38 KB（`MJPEG_POOL_BYTES`）的 bump 分配器，
   一次性 `pvPortMalloc` 出来，每帧复位游标。
3. **`jconfig.h`** 等配置裁剪到只需要解码路径。

**为什么是行钩子**：解压器不知道屏幕几何，也不该假设 surface 在连续显存里。行钩子把
"解压"和"往哪儿放"解耦，顺带让"解码到任意矩形/缩放"变得可能。

**4:4:4 的特殊情况**：`jdmerge.c` 只服务 4:2:0 / 4:2:2（有 upsampler 才有行钩子）。
4:4:4 走 `jdcolor.c` 的 `null_convert`，直接往调用方给的 `JSAMPARRAY` 里写，
所以 `video_mjpeg.c` 另外分配了一条 `scanline = surface_w * 3` 字节的扫描行，
用 `jpeg_read_scanlines()` 逐行读——传 `NULL` 会当场段错误。

每帧流程：`jpeg_read_header()` → `jpeg_start_decompress()` →
`while (output_scanline < output_height)`（4:2:0/4:2:2 时由 upsampler 自己驱动 emit）→
`jpeg_finish_decompress()`。`abort_decompress` 只在出错路径调用。

---

## A4 音频链路（SAI1_A + WM8978）

- SAI1 Block A，I2S 主发，时钟源 PLLI2S，`NoDivider` 必须使能（否则 MCLK 分频错）。
  `FrameInit` / `SlotInit` 必须显式配（FrameLength 64、ActiveFrameLength 32、
  SlotSize 32B、SlotNumber 2、SlotActive 0|1），历史上这三处漏配都表现为"没声音但不报错"。
- 采样率枚举 `s_pll_presets[]`（8 k…192 k 共 13 项，`(rate, pll_n, pll_q, divq)`），
  动态改 `Init.AudioFrequency` 后重新 `HAL_SAI_Init()` + `__HAL_SAI_ENABLE()`。
  11.025 / 22.05 / 44.1 k 用 429/2/18，其余用 344/7/0；88.2 / 176.4 k 用 271/2/2。
- 数据通路：SAI1_A_TX ↔ **DMA2 Stream3**，`HAL_DMAEx_MultiBufferStart()` 双缓冲
  （M0AR/M1AR），循环模式；半区完成由 `HAL_SAI_TxCpltCallback()` 回调上层。
- `app/audio_pcm.c` 把双缓冲包成一个二进制信号量：先 prefill 两个半区，
  再 `bsp_sai_play()` + `bsp_wm8978_unmute()`（顺序反了会听到起播的"咔"）。
- WM8978 走软件 I2C（PH4 = SCL / PH5 = SDA），`bsp_iic.c` 是位翻转实现。

---

## A5 SD 卡读路径（SDIO + DMA2 Stream6）

- 4-bit SDIO，读用 `HAL_SD_ReadBlocks_DMA()`，写保持轮询（写只在 USB MSC/字体初始化时
  出现，量小，TX FIFO 由 CPU 喂没有 overrun 风险）。
- **为什么是 Stream6**：SAI 的音频流占了 DMA2 Stream3，两个 Stream 分开就不必在
  播放与读卡之间来回重置通道。105 的旧版 `bsp_sdio.c` 用的正是 Stream3，直接拷过来会
  和音频打架——这是本次移植最容易踩的坑之一。
- **尾 FIFO 滞留**（真机取证过的坑）：卡的数据通道结束时 SDIO 会撤掉 DMA 请求，
  最后几个字留在 32 字 FIFO 里，流永远等不到 TC。实测 `NDTR` 冻结在 4（每块 16 字节），
  而此刻 `SDIO->FIFOCNT` 读出来是 0 —— 所以**只有 NDTR 是可用的计数**。
  处理顺序不能反：先手工把残留字搬进缓冲区，**再**让 HAL 清 `DCTRL.DTEN`
  （清了就冲掉了），最后 `HAL_SD_Abort()` 收尾（顺带发出多块读需要的 CMD12）。
- 其余错误：`HAL_SD_Abort()` + 重试，`SDIO_IO_RETRIES = 2`（合计 3 次尝试）。读是幂等的，
  重放同一块永远安全；写不重试。
- 中断优先级：SDIO 与 DMA2 Stream6 都是 6，低于 `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`(5)，
  所以 ISR 里可以用 FromISR API。
- 单次突发 `SDIO_CHUNK_BLOCKS = 16` 块（8 KB）；等尾部的静默期 `SDIO_STALL_MS = 3`。
- **突发何时算结束**：DMA 在跑时 `NDTR` 一直在变，卡住不动了才说明这一串结束了。
  所以轮询路径的判据是"`NDTR` 连续 `SDIO_STALL_MS` 毫秒没变化"，不是等某个标志位。
- **DATAEND 早于 HAL 收尾**：如果卡已经报 DATAEND 而 `bsp_sdio_irq_handler()` 没抢到这批
  （它抢到会置 `dma.done`），说明 HAL 已经把 `DCTRL.DTEN` 清掉了，尾巴**已经丢了**——
  此时不能装作成功，交给重试逻辑重放（读幂等）。
- **ISR 抢了尾巴之后**：`SD_DMAReceiveCplt()` 不会再跑，所以必须自己
  `HAL_SD_Abort()` 关掉 DPSM / DMAEN（多块时还发 CMD12），并把 `hsd_card.State` 复位成 READY。
  否则下一次 `HAL_SD_ReadBlocks_DMA()` 会因状态不是 READY 直接返回 BUSY。
- **调度器启动前**：内核把 `BASEPRI` 停在 `configMAX_SYSCALL_INTERRUPT_PRIORITY`，
  6 号中断（SDIO / DMA2 Stream6）全被屏蔽，回调不会来、信号量也不能等，
  这段只能读寄存器轮询。启动期挂载 SD 卡是这条路径的唯一使用者。
- 轮询写完成后还要等卡回到 `HAL_SD_CARD_TRANSFER` 状态再发下一条命令，
  否则下一个 CMD 会被卡的状态机拒掉。
- 重试日志有总量上限 `SDIO_RETRY_LOG_LIMIT = 8`：一次大拷贝能刷出几十条，
  而在拷贝中间打一条阻塞串口日志比重试本身还贵。日志打在 `HAL_SD_Abort()` 之前——
  那个调用会把 `ErrorCode` 清掉。
- FatFs 访问统一套 `fs_lock()` / `fs_unlock()`（`app/fs_diskio.c` 的互斥量），
  因为 UI 任务、播放任务、字体加载都会碰 `0:`。`fs_try_lock()` 给不能无限等的路径用。
- `f_read()` 有一条把调用方缓冲直接交给 diskio 的快路径，而 DMA 要求 16 字节对齐，
  所以不对齐的请求经 `s_sd_scratch` 中转（512 B 对齐到 32 字节）。

---

## A6 播放器任务

- 播放列表：一次 `f_opendir("0:/VIDEO")` + `f_readdir()` 收集所有 `.avi`（上限 32），
  例程用 `dir_sdi()` + `DIR.blk_ofs` 这种 FatFs 内部字段做"从当前目录项继续"，既依赖
  内部布局又不可移植，已废弃。
- 帧节拍：`xTaskGetTickCount()` 累加 `frame_interval_us`，不用 TIM6 中断。
  解码比节拍快就 `vTaskDelay()` 补齐，慢就丢节拍继续（不积压）。
- 一帧的循环：`f_read(chunk + 8)` → 按 `stream_id` 分流：
  `00dc/01dc` → `video_mjpeg_decode()` + `video_ui_frame_ready()`；
  `00wb/01wb` → `audio_pcm_write()`。
- 按键：KEY0 = 下一个文件，KEY2 = 上一个文件，KEY_UP / KEY1 = ±5 s seek；
  扫描只在 `ui_task`（10 ms 周期）里做一次，播放任务不轮询按键。
- 任务参数：`video_player_task` 优先级 3、栈 1536 words；
  `ui_task` 优先级 2（LVGL 归它，`video_ui_lock/unlock` 是递归互斥量）。
- 暂停/切文件时 `audio_pcm_close()` 要等当前半区播完再关 DMA，否则尾巴被截。

---

## A7 控制台（USART1）

- 板载 CH340 接的是 **USART1（PA9/PA10）**，不是 USART3。105 上曾经用 USART3，
  寄存器层面一切正常（TC=1、TXE=1、AF7、BRR=0x16C）但总线没接出去，日志到不了上位机。
- 两条发送路径，按调用时刻选：
  - 调度器**没跑**（启动阶段）→ 阻塞 `HAL_UART_Transmit()`：此时 TXE 中断可能还没使能，
    只入环没人排空，启动横幅会被静默吞掉；
  - 调度器**在跑** → 临界区入环 + TXE 中断排空，用 `xSemaphoreCreateMutex()` 串行化。
- 互斥量**惰性创建**：`BSP_UART_Init()` 早于 `vPortDefineHeapRegions()`，
  在那里建信号量会从未定义的堆里分配。
- TX 环 2048 B（`UART_TX_BUF_SIZE`，必须是 2 的幂，head/tail 回绕靠它）；满了丢字节，
  `uart_write()` 返回值可能小于 `len`。
- RX 环 128 B（`UART_RX_BUF_SIZE`），由 RXNE 中断填、`uart_read()` 取空。
- `printf()` 经 `_write()` → `uart_write()` 重定向，但业务代码统一用 `PRINT_LOG()`
  （`app/log.h` 的编译开关控制）。

---

## A8 GBK 字体（SD 卡字库）

- `.FON` 是标准 GBK 点阵文件，线性索引：
  `index = 190 * (qh - 0x81) + (ql - 0x40 或 0x41)`，`offset = index * bytes_per_glyph`。
- `f_open()` 首次可能返回 `FR_DISK_ERR`（SDIO 数据块传输被 USB 之类的中断打断导致
  CMD 超时），所以 `FONT_OPEN_TRIES = 4`、间隔 `FONT_OPEN_GAP_MS = 5`。
- 文件存储是 **MSB first + 列扫描**，而 LVGL `draw_letter_normal()` 要的是
  **MSB first + 连续行比特流**（行不按字节对齐，`width_bit = box_w * bpp`）。
  `lv_font_gbk.c` 直接从列扫描转到行比特流，不经过驱动那层 LSB/行格式，省一次遍历。
- 每个字号有 LRU 位图缓存（`GBK_CACHE_STAT_T` 记命中/未命中）；不缓存的话一次时钟刷新
  就要重读一遍字库，SD 上的 `f_lseek + f_read` 比转换本身慢几个数量级。
- `UNIGBK.BIN` 提供 Unicode → GBK 的映射；`0x4E00..0x9FA5` 用稠密表
  （`lv_gbk_map.c`，20902 项），其余 889 个用有序表二分查找。生成脚本
  `scripts/gen_gbk_map.py`，**该文件是生成的，不要手改**。

---

## A9 构建

- GCC：CMake + Ninja，`arm-none-eabi-gcc`；Debug `-Og` / Release `-Os`，
  `-Wall -Wextra -Wno-unused-parameter` 零警告。
- MDK：ARMCLANG 6.14 + MicroLIB，`Debug` / `Release` 两个 target 由
  `MDK-ARM/gen_uvision.py` 生成 `.uvprojx`（源集与 `CMakeLists.txt` 必须逐项一致）。
- 第三方与生成代码统一加 `-w`：libjpeg 25 个 `.c` 用 `COMPILE_OPTIONS "-w;-O2"`，
  LVGL 递归 GLOB 同样 `-w`。
- FreeRTOS 的 GCC `port.c` 单独用 `-mfloat-abi=softfp -mfpu=fpv4-sp-d16`，
  避免与内核其余部分（hard-float）在上下文切换处不一致。
- 链接：GCC 侧 `.freertos_heap (NOLOAD) >SDRAM`；MDK 侧同名区段写成 `.bss.freertos_heap`
  （ZI 区），否则启动文件会把它当已初始化数据从 Flash 拷 3 MB。

---

## A10 未验证项（无硬件）

到本次提交为止只有**编译级验收**（GCC Debug/Release、MDK Debug/Release 四份零警告），
以下都还没有真机结论：

- MJPEG 解码的实际帧率、surface 与 LVGL 刷新的撕裂情况；
- SAI/PLLI2S 各采样率的实际输出（预设表按公式算的，未上示波器/听感验证）；
- SDIO 尾 FIFO 处理在**本工程**（Stream6）上的复现与修复效果——A5 的取证来自 104/105；
- 按键 seek 的边界、连续切文件时的内存/句柄泄漏；
- 播放列表上限（32 个文件）与 8.3 长文件名的组合。
