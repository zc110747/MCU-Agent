# 102.stm32f429_tinyusb_ui — 长期项目记忆

## 硬件平台
- MCU: STM32F429IGT6, HSE 25MHz, 内部 SRAM 256KB / SDRAM(W9825G6KH-6 32MB) 经 FMC.
- 调试串口: **USART3 (PB10/PB11) @ 115200 8N1**, 本机枚举为 **COM5** (用户确认 COM5 即通讯串口).
  ⚠️ COM5 的 CH340 会周期性进入 **code-31 / `PermissionError(13)`** 状态，需重新插拔 USB
  才能恢复；机器上还有另一个 CH340 占 **COM4**，但**不是**本板（抓不到任何字节）。
  串口挂掉时可用 SWD 读内存取证（`verify_log_switch.py` 的做法）。
- 烧录: OpenOCD + ST-Link; openocd 在 `D:/software/ST/OpenOCD/bin/openocd.exe`, scripts 在 `D:/software/ST/OpenOCD/share/openocd/scripts`; cfg=`interface/stlink.cfg`+`target/stm32f4x.cfg`.
- USB: TinyUSB Host (U 盘 MSC), FatFs 卷 **`0:`**; 字库在 `0:/SYSTEM/FONT/` (GBK12/16/24/32.FON + UNIGBK.BIN).
- microSD: **SDIO 4-bit**, PC8=D0 PC9=D1 PC10=D2 PC11=D3 PC12=CK PD2=CMD (AF12, 上拉), FatFs 卷 **`1:`**,
  `FF_VOLUMES=2`; SDIOCLK=48MHz(PLLQ=7, 与 USB 同源), ClockDiv=2 → 卡时钟 12MHz, **轮询不接 DMA**.
  这些引脚与 FMC 不冲突（FMC 只占 PC0/2/3 与 PD0/1/8/9/10/14/15）。
- **HAL 时基 = TIM7** (`TIM7_IRQn=55`, 独立向量; 已弃用 TIM11/TIM1_TRG_COM)。FreeRTOS 用 SysTick。
- 启动加载器: ASCII 启动页 `wait for system start...` → 先试 SD(1:) → 失败等 USB(0:) → 字库就绪进
  主界面 / **10s 超时** 居中 `sdcard and usb loader failed!`（超时后仍 3s 静默探测，热插拔可恢复）。
- LCD: **800x480** 正点原子, FMC Bank1 NE1 8080 16-bit (RS=A18, LCD_BASE=0x60000000|0x0007FFFE); 控制器 NT35510(0x8000)/ILI9806E 回退.
- **`lcd_scan_dir` 的宽高交换逻辑是 正点原子 原版、正确，切勿改反/删除**: `DFT_SCAN_DIR=L2R_U2D`(MV=0) 下, 因 `lcd_width(800)>lcd_height(480)` 触发交换 → **有效 GRAM 窗口 480x800**, 这正是 NT35510 模块铺满物理 800x480 屏所需的窗口. 屏幕尺寸只由 `LCD_WIDTH/LCD_HEIGHT`(`bsp/bsp_lcd.h`) 决定, 不要动交换逻辑.
- **LVGL 画布 = GRAM 窗口 = 480x800**（不是 800x480）。UI 布局已从 `lv_disp_get_hor_res/ver_res()`
  自适应取，不要再硬编码 800x480。
- 电容触摸: **GT9147/GT911**，走**软件位绑定 I2C**（不是 SPI，芯片根本没有 SPI 模式）。
  排针对应: `T_SCK(PH6)=CT_SCL`、`T_MOSI(PI3)=CT_SDA`、`T_CS(PI8)=CT_RST`、`T_PEN(PH7)=CT_INT`、
  `T_MISO(PG3)` 未用。**板上实贴是 GT911**（`product ID="911" addr=0x14`），自报分辨率 **480x800**
  与画布一致 → 恒等映射。184B 配置块是 9147 专用，**绝不能给 GT911 上传**。
- I2C2 (PH4/PH5): AP3216C(0x1E) + MPU9250(0x68, 内含 AK8963 0x0C) + PCF8574(0x20) + AT24C02(0xA0)。
  传感器由 `app/sensor_task.c` 每 500ms 采样。
  **本板 MPU9250 无可用磁力计**（`AK8963 WIA=0x00`，兼容片），磁力计必须按"可选"处理，
  不要把 0.0 uT 当成功上报。
- **PH7 (T_PEN) 极易产生中断风暴**：浮空输入 + 紧邻 PH6(位绑定 SCL 165 kHz) 串扰，
  实测 **46 925 次/秒**。必须上拉；且 ISR 内立即屏蔽 line 7、任务侧消抖后重新武装。
  风暴会抢占低优先级的轮询式 I2C 传输并**锁死 I2C2 总线**（见下方"已知坑"）。

## 日志约定（用户明确指令，2026-08-29）
- **应用日志一律用 `PRINT_LOG(...)`（`app/log.h`），不要写裸 `printf()`**；
  `snprintf` 属字符串格式化，保留不动。
- 全局开关 `PRINT_LOG_ENABLE`；构建层用 `cmake -DENABLE_PRINT_LOG=OFF`，
  CMake 传 `-DPRINT_LOG_ENABLE=0|1`。关闭后所有日志编译成空语句（省 ~6 KB FLASH）。
- `PRINT_LOG` **不能在 ISR 里调**（内部拿互斥量）；中断上下文用 `uart_write()`。
- 二进制/无 NUL 的内容 dump 不要走 `PRINT_LOG`（当格式串遇到 `%s` 会崩），
  保持 `uart_write()` 直连并自己用 `#if PRINT_LOG_ENABLE` 包住。
- **`BSP_UART_Init()` 早于 SDRAM/`vPortDefineHeapRegions()`**，所以 TX 互斥量
  **不能在 init 里创建**（会破坏尚未定义的堆），改成调度器运行后惰性创建。

## 工作流约定（用户明确指令）
- **每次改完代码 → 直接构建 + OpenOCD 烧录 + COM5 串口真机验证，不再只交付 ELF 让用户手动跑**。环境（COM5/OpenOCD/ST-Link）已就绪。
- 验收铁律（见 stm32-verification-acceptance skill）: 先计划后编码 → Debug/Release 双构零警告(显式列 RAM/FLASH 占比) → 真机烧录(Verified OK) → verify 脚本 pass/fail → 增量交付清单(✅ 收尾) → README 更新。
- 真机验证命令: `SERIAL_PORT=COM5 python verify_serial/verify_ui_com5.py` (脚本内含烧录+抓串口); 或纯烧录用 openocd `flash write_image erase ...` + `verify_image` + `reset run`.

## 已知坑
- `lv_conf.h` 曾把 `LV_MEM_ADR` 重定义成 0（会让 LVGL 在内部 SRAM 塞 256KB 静态 BSS），必须保持 `0xC0100000U` (SDRAM, 避开 FreeRTOS 堆 0xC0000000~0xC007FFFF)。
- `ffconf.h` 须 `FF_FS_EXFAT 1` (U 盘 exFAT 才能挂载字库)。
- `stm32f4xx_hal_conf.h` 须开 `HAL_SRAM_MODULE_ENABLED` (FMC TFT 依赖)。
- LCD 地址窗口必须用 MIPI-DCS 时序: 命令写一次 + 跟 4 数据字节; 不可把 0x2A/0x2B/0x2C 当连续寄存器拆写 (否则渲染带写到错乱 GRAM → 文字重叠)。
- 本机 COM5 曾出现驱动 code-31 (设备未发挥作用); 用户侧已恢复, 视作可用.
- **OpenOCD 烧录必须用 `.elf`**(自带加载段), 用 `.bin` 会报 `no flash bank found for address 0x00000000` 且 `wrote 0 bytes`. 命令: `flash write_image erase build/stm32f429_tinyusb_ui.elf` + `verify_image` + `reset run`. (注意 Git-Bash 里 cd 路径必须用正斜杠, 反斜杠会吞掉目录分隔.)
- **UART TX 环形缓冲满会静默丢字节**: `bsp_uart.c` 的 `uart_write()` 满即 `break`。`UART_TX_BUF_SIZE`
  已从 512 提到 **2048**, 但任何大批量打印仍会丢其它任务的日志。新增批量输出时务必限流
  (`usb_host_app.c` 的 dump 已加: >2048B 文件只列目录项、整轮预算 16KB)。
- **LVGL 切画面必须先删定时器再清屏**: `app_ui.c` 的 `ui_teardown()` 先 `lv_timer_del(s_timer)`
  再 `lv_obj_clean(lv_scr_act())`; 顺序反了定时器会持有已释放 `lv_obj_t*` → 下一 tick 硬 fault。
- **状态机守卫别写 `!= 目标态`**: 超时分支曾写成 `s_state != LOAD_OK`, 而 `LOAD_FAILED` 也满足该式
  → 每 5ms 重复触发。应写成 `s_state == LOAD_BOOT`。
- **SDIO 目标缓冲必须 4 字节对齐**: HAL 内以 `uint32_t*` 读 SDIO FIFO; `fs_diskio.c` 对未对齐地址
  统一经 `s_sd_scratch[512]` 中转, 勿删。
- 改完代码后**先烧录再抓串口**: `tools/verify_serial/capture_reset.py` 只复位不烧录, 否则看到的是旧固件行为。
- **跨任务读 LCD 几何要先等 `lcd_driver_ready()`**: `g_lcd_info` 由 `ui_task` 里的
  `lcd_driver_init()` 填，优先级更高的任务（如 touch_task）先跑会读到全 0（表现为 `canvas 1x1`）。
- **MPU9250 返回值 `-3` 只代表磁力计失败**: `bsp_mpu9250_read()` 的 `-1`/-2` 是加速度/陀螺失败,
  `-3` 是 AK8963(经 MPU 内部 I2C master)失败, 此时 accel/gyro 数据是好的, 不能整包丢弃。
- **限流打印的哨兵别用 `0xFFFFFFFFU`**: `counter - 0xFFFFFFFF` 会回绕成小正数, 导致首条日志
  永远打不出来。用 `0` 作哨兵并加 `*last == 0` 短路。
- **GT9xx 中断后必须轮询到抬手**: 不同模组 INT 行为不同(单次脉冲 / 持续脉冲), 中断只当唤醒,
  任务随后以 15ms 轮询直到连续 3 次无触点。
- **无需手指即可验证 EXTI 链路**: `EXTI->SWIER`(0x40013C10) 写 1 产生与引脚边沿等价的中断。
  OpenOCD `halt` → `mww 0x40013C10 0x80` → `resume`（已封装 `verify_touch_irq.py`）。
- newlib-nano 未链 `-u _printf_float`, **`printf`/`PRINT_LOG`/`lv_label_set_text_fmt` 不能用 `%f`**；
  需要小数时手工拆成整数+%02d（见 `app_ui.c` 的 `fmt_fixed2()`）。
- **OpenOCD `mdw` 读非 4 字节对齐地址会报 `Failed to read memory`**。
  读 `uint8`/`uint16` 混排的静态变量时要按 4 字节对齐整字读，再在 Python 里切字节。
- **残留 openocd.exe 会让 ST-Link 报 `libusb_open() failed with LIBUSB_ERROR_ACCESS`**，
  先 taskkill 再烧。
- **轮询式 HAL 传输（无 DMA 的 I2C/SPI）会被高优先级任务的忙等抢占而超时**，
  超时后**从机仍拉住 SDA、外设锁在 BUSY** → 必须调 `BSP_I2C_Recover()`，
  否则一次失败变永久失败。防护三件套：加大 timeout(10→50ms) +
  `vTaskSuspendAll()` 包成原子操作 + 失败先 recover 再立即重试一次。
- **"没报错" ≠ "有数据"**：错误日志还是限流的，正向验证要用
  `arm-none-eabi-nm` 取址 + OpenOCD `mdw` 直读目标内存里的数据结构
  （`tools/verify_serial/verify_sensors.py`）。
- **给所有外部中断加"速率看门狗"**（1 s 窗口计数、超阈值打一条 WARNING）：
  实测把"中断风暴"从猜测变成 46925/s 这样的数字，是这类问题最划算的诊断投入。
- **噪声中断的正解是 ISR 内屏蔽 + 任务侧延时重新武装**（~47 kHz → ~20 Hz），
  不是在 ISR 里做软件滤波。
