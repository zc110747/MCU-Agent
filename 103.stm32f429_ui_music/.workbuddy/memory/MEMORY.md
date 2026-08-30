# 103.stm32f429_ui_music 项目长期记忆

## 定位
STM32F429IGT6 音乐播放器（UI 显示 + WM8978 音频），从 `102.stm32f429_tinyusb_ui` 移植：
USB FS Host(TinyUSB) / microSD 读 U 盘或 SD 的 `music/` 目录 `.wav`/`.mp3`，
WM8978 经 SAI1(I2S 主发) 播放，LVGL 深色 UI，FatFs exFAT，FreeRTOS 堆在 SDRAM。

## 铁律 / 约定
- 零警告硬约束：`-Wall -Wextra`，Debug + Release 双构零警告（验收显式列 FLASH/RAM/SDRAM 占比）。
- 点击死机**真因（实锤，勿再回到旧结论）**：`DMA2_Stream3_IRQHandler` 无强定义 → 是 `Default_Handler` 的 weak 别名；播放一启动 `HAL_SAI_Transmit_DMA` 使能 DMA 流，中断即跳 `b .` 死循环 → 整板冻结。属「未处理 IRQ」而非 fault，故 **CFSR=0/HFSR=0**；触摸 ISR 优先级更高仍可抢占 → 「坐标照刷但 UI 冻」。证据：PC=0x0800ee38(`e7fe`)、ICSR=0x04c4784b→IRQn=59。修复：`bsp_sai_audio.c` 增该 ISR 调 `HAL_DMA_IRQHandler(&hdma_sai)`。
  - ~~minimp3 16 KB scratch 栈溢出 / 递归互斥量~~ 均已**证伪**（栈溢出应是 CFSR≠0 的 HardFault，与实测 0 不符）；相关结构性改造仍有加固价值，保留。
- **铁律：MP3 解码只许在 `player_task` 栈（32 KB）执行，ui_task 永不解码。** `player_play`/`player_load_locked(autoplay)` 仅置 `PLAYER_PRIMING`+`s_need_prime`，由 `player_task` 循环开头真正 prime/refill；`audio_decoder_open` 用 `mp3_parse_header` 廉价取采样率（不调 `mp3dec_decode_frame`）。新增 decode 路径前必须先确认不在 ui_task 调用链上。
- 中文曲名：`FF_LFN_UNICODE=2`（FatFs 返 UTF-8）匹配 `lv_font_gbk`；`FF_CODE_PAGE` 保持 936。
- 屏 2 音乐 UI 用 `lv_bar` + 可点击轨道自绘（`lv_slider` 在本工程 lv_conf 关闭）。进度滑动栏 `track_y=m_h*0.37`、右侧音量面板 `vpy=m_h*0.35`（2026-08-30 下移 ~7%）。
- **调试串口 USART3(PB10/PB11, 115200 8N1) 是 TX 单向**：本板调试座 RX(PB11) 未接板上 USB-UART，主机→板字符到不了（`uart_getchar_nowait` 恒空），**只能打印不能收命令**（SWD 已证：PB11 恒高/RXNE 永不置位，但 CR1=0x200C/PB10-11 均 AF7）。故串口命令需经 **SWD 邮箱注入**：写 NUL 串到 `g_dbg_line`@0x20006448 + 置 `g_dbg_pending`@0x20006444，`serial_cmd_task` 轮询即走同分发。`auto_verify_ocd.py` 即此法做无人干预验收（实跑 13/13 PASS）。
- 解码统一归一化为 16-bit 有符号立体声 L/R 交错（匹配 SAI DMA 半字）。
- WM8978 I2C 从机 `0x1A`，与传感器共用 I2C2（PH4/PH5），沿用总线恢复/临界区防护。

## 硬件关键
- WM8978: SAI1_A PE2(MCK)/PE4(FS)/PE5(SCK)/PE6(SD)；I2C2 PH4/PH5；PWM_AUDIO PA3。
- SAI 时钟：F429 经 `RCC_PERIPHCLK_SAI_PLLSAI` + `PLLSAI`（无 Sai1ClockSelection 成员）。
  **公式（源自 `stm32f4xx_hal_sai.c:456/465`）**：`MCLK=SAI_CK/(MCKDIV×2)`、`MCLK=256×FS`
  → **`FS=SAI_CK/(MCKDIV×512)`**（与帧长无关），`BCLK=FS×32`。
  只要 `AudioFrequency != SAI_AUDIO_FREQUENCY_MCKDIV`，**HAL 必覆盖 `Init.Mckdiv`**（写死即死代码）。
- **按族动态配 PLLSAI**（PLLSAI 本板只供 SAI：USB/SDIO 48 MHz 走主 PLL 的 PLLQ，LTDC 未使能）：
  44.1k 族 N271/Q2/D6=22.583333 MHz（44100/22050/11025，MCKDIV 1/2/4）；
  48k 族 N172/Q7/D1=24.571429 MHz（48000/24000，MCKDIV 1/2）；
  32k 族 N213/Q13/D1=16.384615 MHz（32000/16000/8000，MCKDIV 1/2/4）。最差 0.36 音分。
- **三个曾踩的静默坑**：① `SAI_MASTERDIVIDER_DISABLE` 实为 NODIV=1（**旁路** MCLK 分频器），要分频须用 `SAI_MASTERDIVIDER_ENABLE`；② `FrameInit`/`SlotInit` 不初始化会导致 SLOTEN=0+FRL=255，须调 `HAL_SAI_InitProtocol()`（其内部已调 `HAL_SAI_Init`）；③ `RCC_DCKCFGR[12:8]=DivQ-1`，解码要 +1（N/Q 是直接值）。
- FS 自测量用空闲 **TIM2**（32 位自由运行）；**不用 DWT->CYCCNT**（空闲任务 WFI 时停走会高估）。结果在 `g_sai_fs_measured_hz`，SWD 可读。

## 待硬件标定
- 真机出声/听感、触摸交互待板验证；MP3/VBR 时长近似（可加 Xing/VBRI 头解析）。
  ~~MCK/BCLK/LRCK 分频~~ **已用 SWD 验证通过**（`tools/audio/verify_sai_clock.py` 12/12 PASS）。

## 工具链
arm-none-eabi-gcc 15.3.1 + cmake 4.2.1 + ninja；cmake --preset debug/release；OpenOCD+STLink 烧录。
