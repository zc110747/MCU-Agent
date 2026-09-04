# STM32H743 SD 卡 JPEG 轮播

从 SD 卡 `image/` 目录读取 JPG 图片，裁剪缩放后每 5 秒刷新一张到 240×240 SPI OLED（ST7789）。

- **MCU**：STM32H743ZIT6（鹿小班 LXB743ZI-P1 核心板），HSE 25MHz → SYSCLK 480MHz
- **工具链**：arm-none-eabi-gcc + CMake + Ninja
- **调试**：ST-Link + OpenOCD + cortex-debug（VSCode 内单步仿真）

## 硬件连接

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| SPI6_NSS | PG8 | 硬件 NSS |
| SPI6_SCK | PG13 | 60MHz（PCLK4 120MHz / 2） |
| SPI6_MOSI | PG14 | 1-line 单工，只发不收 |
| LCD_DC | PG15 | 数据/命令选择 |
| LCD_BL | PG12 | 背光，高有效 |
| LED | PG7 | 每次换图翻转一次，作心跳指示 |
| SDMMC1 | PC8–PC12、PD2 | 4-bit 总线 |
| USART1 | PA9 / PA10 | 115200-8-N-1，非阻塞 PRINT_LOG 日志（环形缓冲 + TXE 中断） |

## 目录结构

```
Core/           启动文件、main、HAL MSP、中断向量、syscalls
Drivers/        CMSIS + STM32H7xx HAL（原样引入，不改动）
third_party/    fatfs (R0.16)、tjpgd (R0.03)
bsp/            板级驱动：OLED、SD 卡、非阻塞 PRINT_LOG 日志（环形缓冲 + TXE 中断）
app/            应用层：JPEG 解码流水线、轮播逻辑
cmake/          arm-none-eabi 工具链文件
tools/          STM32H743.svd（调试时查看外设寄存器）
```

## 构建

```bash
cmake --preset debug          # 配置
cmake --build --preset debug  # 编译
```

产物在 `build/debug/`：`stm32_sd_oled.elf` / `.hex` / `.bin` / `.map`。

Release 版把 `debug` 换成 `release` 即可（`-O2`）。

## 烧录与调试

VSCode 中：

- `Ctrl+Shift+B` → 默认任务 **build (debug)**
- 任务 **flash (ST-Link / OpenOCD)** 直接烧录
- `F5` → **Debug (ST-Link + OpenOCD)**，自动编译并停在 `main`

命令行烧录：

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
        -c "program build/debug/stm32_sd_oled.elf verify reset exit"
```

## SD 卡准备

- 文件系统：FAT32（`FF_MAX_SS = 512`，标准 SD/SDHC 卡即可）
- 目录：卡根目录下新建 `image/`
- 图片：任意尺寸的基线（baseline）JPEG，扩展名 `.jpg` / `.jpeg`，大小写不限
- 最多识别 64 张，文件名（含扩展名）不超过 63 字符

```
SD:/
└── image/
    ├── 001.jpg
    ├── 002.jpg
    └── ...
```

> 渐进式（progressive）JPEG 不受支持，TJpgDec 会返回 `unsupported format`，
> 该图会被跳过并在屏幕上提示，不影响后续轮播。

## 图像处理流水线

```
f_read ──▶ jd_prepare ──▶ 选择 1/1、1/2、1/4、1/8 硬解降采样
                              │  （取仍能覆盖 240×240 的最大降采样比）
                              ▼
                         jd_decomp ──▶ 逐 MCU 块输出
                              │
                              ▼
                    中心裁剪成正方形 + 反向映射（gather）重采样
                              │
                              ▼
                240×240 RGB565 帧缓冲 ──▶ LCD_CopyBuffer（SPI 16-bit）
```

先让解码器做大部分缩小（近乎免费），再对裁剪后的正方形做整数反向映射。
反向映射按目标像素回查源像素，因此**不会出现前向散射常见的空洞或接缝**；
已用 10 组不同长宽比/MCU 尺寸仿真验证，240×240 每个像素恰好被写入一次。

## 内存占用

| 区域 | 占用 | 容量 |
| --- | --- | --- |
| FLASH | 58304 B (2.78%) | 2 MB |
| RAM_D1 (AXI SRAM) | 163424 B (31.17%) | 512 KB |

其中帧缓冲 112.5 KB、TJpgDec 工作区约 14 KB。DTCM / RAM_D2 / RAM_D3 / ITCM 全部空闲，
链接脚本已预留 `.dtcm`、`.ram_d2`、`.ram_d3` 段方便后续扩展（例如加 LVGL）。

## 关于 Cache

SDMMC 与 SPI 均使用 CPU 轮询模式（`HAL_SD_ReadBlocks` 从 FIFO 逐字搬运），
不涉及 DMA，因此 I-Cache / D-Cache 全开也没有一致性问题，
MPU 只保留背景映射，无需配置 non-cacheable 窗口。
后续若改用 DMA（SDMMC IDMA 或 SPI DMA），需要为对应缓冲区补上 MPU 配置或 cache 维护操作。

## 日志系统（PRINT_LOG）

`bsp/bsp_log.{c,h}` 提供非阻塞日志：`printf_log()` 是 `printf()` 的直接替代，先把格式化结果写入栈上
缓冲，再推入发送环形缓冲（1 KB）；USART1 的 **TXE 发送完成中断**在后台逐字节把环形缓冲排空，
**调用方永不阻塞 UART**。

- 应用统一入口：`PRINT_LOG("fmt\r\n", ...)`（原先的 `LOG_I/LOG_W/LOG_E` 宏已删除，统一走此入口）。
- 关闭输出：构建时加 `-DPRINT_LOG_ENABLE=0`，所有 `PRINT_LOG(...)` 被编译消除
  （死代码消除，零运行时开销、无 UART 流量）。`bsp_log.h` 默认 `PRINT_LOG_ENABLE 1`。
- 约束：`PRINT_LOG` / `printf_log` 仅限线程模式调用，**禁止在 ISR 中使用**；
  中断侧由 `USART1_IRQHandler → log_uart_tx_irq()` 自动 drain 环形缓冲。
- 多缓冲安全：写环形缓冲时关闭 TXE 中断以保护共享索引；发送空闲时由 `uart_write` 触发首字节，ISR 排空剩余。
- `printf()` 经 newlib `_write` 重定向也走同一非阻塞环形缓冲（兼容散落的 `printf`）。

## 运行日志示例

```
[I] ==============================================
[I]  STM32H743 SD-card JPEG slideshow
[I]  SYSCLK    : 480000000 Hz
[I]  Build     : Sep  4 2026 15:45:26
[I] ==============================================
[I] SD card: type=1, blocks=62586880, blocksize=512, capacity=30560 MB
[I] FatFs volume 1: mounted
[I] scanning 1:/image ...
[I]   [ 0] 001.jpg                          86871 bytes
[I]   [ 1] 002.jpg                          17380 bytes
[I]   [ 2] 003.jpg                          213773 bytes
[I] 3 jpeg file(s) in 1:/image
[I] [1/3] 001.jpg  768x480 -> 1/2 -> crop 240 -> 240x240  97ms
```

## 异常处理

| 情况 | 表现 |
| --- | --- |
| 未插卡 / 挂载失败 | 屏幕显示 `NO SD CARD`，每 5 秒自动重试挂载 |
| `image/` 为空 | 屏幕显示 `NO PICTURES`，每 5 秒重新扫描 |
| 单张图片解码失败 | 屏幕提示 `DECODE FAILED` + 文件名，下一周期继续播放后面的图 |
| 运行中拔卡 | 回到重试模式，插回后自动恢复轮播 |
