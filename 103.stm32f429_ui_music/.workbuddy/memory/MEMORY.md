# 103.stm32f429_ui_music 项目长期记忆

## 定位
STM32F429IGT6 音乐播放器（UI 显示 + WM8978 音频），从 `102.stm32f429_tinyusb_ui` 移植：
USB FS Host(TinyUSB) / microSD 读 U 盘或 SD 的 `music/` 目录 `.wav`/`.mp3`，
WM8978 经 SAI1(I2S 主发) 播放，LVGL 深色 UI，FatFs exFAT，FreeRTOS 堆在 SDRAM。

## 铁律 / 约定
- 零警告硬约束：`-Wall -Wextra`，Debug + Release 双构零警告（验收显式列 FLASH/RAM/SDRAM 占比）。
- 点击死机已通过递归互斥量 `s_lock` 修复：`audio_player.c` 所有 `s_dec`/`s_state` 访问须 `PLOCK_TAKE/PLOCK_GIVE`；`configUSE_RECURSIVE_MUTEXES=1` 是前提。`player_task` 先无锁等空半区、拿锁后复核再解码。
- 中文曲名：`FF_LFN_UNICODE=2`（FatFs 返 UTF-8）匹配 `lv_font_gbk`；`FF_CODE_PAGE` 保持 936。
- 屏 2 音乐 UI 用 `lv_bar` + 可点击轨道自绘（`lv_slider` 在本工程 lv_conf 关闭）。
- 解码统一归一化为 16-bit 有符号立体声 L/R 交错（匹配 SAI DMA 半字）。
- WM8978 I2C 从机 `0x1A`，与传感器共用 I2C2（PH4/PH5），沿用总线恢复/临界区防护。

## 硬件关键
- WM8978: SAI1_A PE2(MCK)/PE4(FS)/PE5(SCK)/PE6(SD)；I2C2 PH4/PH5；PWM_AUDIO PA3。
- SAI 时钟：F429 经 `RCC_PERIPHCLK_SAI_PLLSAI` + `PLLSAI`（无 Sai1ClockSelection 成员）；
  PLLSAIN/PLLSAIQ/PLLSAIDivQ + Mckdiv 为硬件标定初值（见 README §8.2）。

## 待硬件标定
- MCK/BCLK/LRCK 分频与音调需示波器+听感确认；MP3/VBR 时长近似；真机出声/触摸交互待板验证。

## 工具链
arm-none-eabi-gcc 15.3.1 + cmake 4.2.1 + ninja；cmake --preset debug/release；OpenOCD+STLink 烧录。
