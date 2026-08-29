---
name: stm32f429-tinyusb-ui
description: 102.stm32f429_tinyusb_ui 项目专属事实库：STM32F429IGT6 + TinyUSB 主机(U盘 MSC) + FatFs 字库 + LVGL v8 + GT9147/GT911 电容触摸(软件位绑定 I2C) + I2C2 传感器(AP3216C/MPU9250) 的引脚、时钟、构建烧录命令、验收脚本与硬件坑。供本工程内任何修改/调试前快速对齐，避免重复查原理图。触发词：102 项目、stm32f429_tinyusb_ui、本工程引脚、COM5、USART3 PB10、GT9147、GT911、T_PEN PH7、I2C2 PH4、SDRAM、LVGL 画布 480x800、启动加载器、verify_sensors、verify_touch_irq、verify_log_switch、verify_boot_flow。
agent_created: true
---

# 102.stm32f429_tinyusb_ui 项目事实库

> 项目路径：`E:/cnb/git/Mcu_Project_Design_By_Agent/102.stm32f429_tinyusb_ui`
> 跨项目通用外设速查见 `stm32-peripheral-drivers`；日志系统见 `stm32-logging-print-log`；
> SWD 取证/中断风暴见 `stm32-swd-forensics`。本文件只记**本项目独有**的事实。

## 一、硬件平台（真机已确认）

- MCU：STM32F429IGT6，HSE 25 MHz，内部 SRAM 256 KB / SDRAM(W9825G6KH-6 32 MB) 经 FMC。
- 调试串口：**USART3 (PB10/PB11) @ 115200 8N1**，本机枚举 **COM5**（已确认即通讯口）。
  ⚠️ COM5 的 CH340 会周期性进 **code-31 / `PermissionError(13)`**，需重插拔恢复；
  机器还有另一个 CH340 占 **COM4**，**不是**本板（抓不到任何字节）。串口挂掉时用 SWD 取证。
- 烧录：OpenOCD + ST-Link；openocd 在 `D:/software/ST/OpenOCD/bin/openocd.exe`，
  scripts 在 `D:/software/ST/OpenOCD/share/openocd/scripts`；
  cfg = `interface/stlink.cfg` + `target/stm32f4x.cfg`。
- USB：TinyUSB Host（U 盘 MSC），FatFs 卷 **`0:`**；字库在 `0:/SYSTEM/FONT/`
  （GBK12/16/24/32.FON + UNIGBK.BIN）。
- microSD：**SDIO 4-bit**，PC8=D0 PC9=D1 PC10=D2 PC11=D3 PC12=CK PD2=CMD (AF12, 上拉)，
  FatFs 卷 **`1:`**，`FF_VOLUMES=2`；SDIOCLK=48 MHz(PLLQ=7, 与 USB 同源)，ClockDiv=2 → 12 MHz，
  **轮询不接 DMA**。引脚与 FMC 不冲突（FMC 只占 PC0/2/3 与 PD0/1/8/9/10/14/15）。
- **HAL 时基 = TIM7**（FreeRTOS 用 SysTick）。
- 启动加载器：ASCII 启动页 `wait for system start...` → 先试 SD(1:) → 失败等 USB(0:) →
  字库就绪进主界面 / **10 s 超时** 居中 `sdcard and usb loader failed!`（超时后仍 3 s 静默探测，
  热插拔可恢复）。
- LCD：**800x480** 正点原子，FMC Bank1 NE1 8080 16-bit (RS=A18, LCD_BASE=0x60000000|0x0007FFFE)；
  控制器 NT35510(0x8000)/ILI9806E 回退。
  ⚠️ `lcd_scan_dir` 宽高交换逻辑是**正点原子原版、正确**，切勿改反/删除：
  `DFT_SCAN_DIR=L2R_U2D`(MV=0) 下因 `lcd_width(800)>lcd_height(480)` 触发交换 →
  **有效 GRAM 窗口 480x800**，正是 NT35510 铺满物理 800x480 屏所需窗口。
- **LVGL 画布 = GRAM 窗口 = 480x800**（不是 800x480）。UI 布局从 `lv_disp_get_hor_res/
  ver_res()` 自适应，不要再硬编码 800x480。
- 电容触摸：**GT9147/GT911**，走**软件位绑定 I2C**（非 SPI，芯片无 SPI 模式）。
  排针：T_SCK(PH6)=CT_SCL、T_MOSI(PI3)=CT_SDA、T_CS(PI8)=CT_RST、T_PEN(PH7)=CT_INT、
  T_MISO(PG3) 未用。**板上实贴 GT911**（`product ID="911" addr=0x14`），自报分辨率 **480x800**
  与画布一致 → 恒等映射。184 B 配置块是 9147 专用，**绝不能给 GT911 上传**。
- I2C2 (PH4/PH5)：AP3216C(0x1E) + MPU9250(0x68, 内 AK8963 0x0C) + PCF8574(0x20) +
  AT24C02(0xA0)。传感器由 `app/sensor_task.c` 每 500 ms 采样。
  ⚠️ 本板 MPU9250 **无可用磁力计**（AK8963 WIA=0x00，兼容片），磁力计必须按"可选"处理。

## 二、构建 / 烧录 / 验证命令

```bash
# Debug 构建（日志开）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# Release 构建（日志开）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Release 关日志（省 ~6 KB FLASH）
cmake -S . -B build_nolog -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_PRINT_LOG=OFF
cmake --build build_nolog

# 烧录（OpenOCD 必须用 .elf，自带加载段；用 .bin 会报 no flash bank found）
openocd -s "D:/software/ST/OpenOCD/share/openocd/scripts" \
  -f interface/stlink.cfg -f target/stm32f4x.cfg \
  -c init -c "reset halt" \
  -c "flash write_image erase build/stm32f429_tinyusb_ui.elf" \
  -c "verify_image build/stm32f429_tinyusb_ui.elf" -c "reset run"

# 真机验收脚本（tools/verify_serial/）
python tools/verify_serial/verify_boot_flow.py    # 启动加载器 17/17
python tools/verify_serial/verify_sensors.py      # SWD 读 s_data 正向验证 7/7
python tools/verify_serial/verify_touch_irq.py    # EXTI_SWIER 软注入验链路 7/7
python tools/verify_serial/verify_log_switch.py  # SWD 读 TX 环验证 PRINT_LOG 开关 6/6
```

## 三、本项目已落地的坑（改动前先读）

- `lv_conf.h` 的 `LV_MEM_ADR` 必须保持 `0xC0100000U`（SDRAM，避开 FreeRTOS 堆
  0xC0000000~0xC007FFFF）；曾被人重定义成 0 会在内部 SRAM 塞 256 KB 静态 BSS。
- `ffconf.h` 须 `FF_FS_EXFAT 1`（U 盘 exFAT 才能挂载字库）。
- `stm32f4xx_hal_conf.h` 须开 `HAL_SRAM_MODULE_ENABLED`（FMC TFT 依赖）。
- LCD 地址窗口必须用 MIPI-DCS 时序（命令一次 + 4 数据字节），不可把 0x2A/0x2B/0x2C
  当连续寄存器拆写（渲染带写到错乱 GRAM → 文字重叠）。
- UART TX 环形缓冲满会**静默丢字节**；`UART_TX_BUF_SIZE` 已从 512 提到 **2048**，
  仍须限流大批量打印。
- LVGL 切画面必须**先删定时器再清屏**：`ui_teardown()` 先 `lv_timer_del(s_timer)` 再
  `lv_obj_clean(lv_scr_act())`；顺序反了定时器持有已释放对象 → 下一 tick 硬 fault。
- 状态机守卫别写 `!= 目标态`（超时分支曾写成 `s_state != LOAD_OK`，`LOAD_FAILED` 也满足 →
  每 5 ms 重复触发）；应写 `s_state == LOAD_BOOT`。
- SDIO 目标缓冲必须 **4 字节对齐**：HAL 内以 `uint32_t*` 读 FIFO；diskio 对未对齐地址
  经 `s_sd_scratch[512]` 中转。
- 改完代码**先烧录再抓串口**：`capture_reset.py` 只复位不烧录，否则看到旧固件行为。
- 跨任务读 LCD 几何要先等 `lcd_driver_ready()`：`g_lcd_info` 由 `ui_task` 里的
  `lcd_driver_init()` 填，高优先级任务先跑会读全 0（表现为 canvas 1x1）。
- MPU9250 返回值 `-3` **只代表磁力计失败**，accel/gyro 数据是好的，不能整包丢弃。
- 限流打印哨兵别用 `0xFFFFFFFFU`（回绕成小正数，首条永远打不出）；用 `0` 作哨兵 +
  `*last == 0` 短路。
- GT9xx 中断后必须轮询到抬手（15 ms 轮询，连续 3 次无触点）；中断只当唤醒。
- newlib-nano 未链 `-u _printf_float`，**`printf`/`PRINT_LOG`/`lv_label_set_text_fmt`
  不能用 `%f`**；需小数时手工拆整数 + `%02d`（`app_ui.c` 的 `fmt_fixed2()`）。
- OpenOCD `mdw` 读非 4 字节对齐地址会报 `Failed to read memory`；按 4 字节对齐整字读，
  Python 侧切字节。
- 残留 openocd.exe 让 ST-Link 报 `LIBUSB_ERROR_ACCESS`，先 taskkill 再烧。
- 轮询式 HAL 传输会被高优先级任务忙等抢占而超时，超时后**从机仍拉 SDA、外设锁 BUSY**
  → 必须调 `BSP_I2C_Recover()`，否则一次失败变永久失败（防护见 `stm32-swd-forensics` §七）。
- "没报错" ≠ "有数据"：错误日志还是限流的，正向验证用 `arm-none-eabi-nm` 取址 +
  OpenOCD `mdw` 直读目标内存里的数据结构（`verify_sensors.py`）。
- 给所有外部中断加"速率看门狗"（1 s 窗口计数、超阈值打 WARNING）：实测把"中断风暴"
  从猜测变成 46925/s 这样的数字，是这类问题最划算的诊断投入。
- 噪声中断正解 = ISR 内屏蔽 + 任务侧延时重武装（~47 kHz → ~20 Hz），不是 ISR 内软件滤波。

## 四、目录布局（app/bsp/Drivers/third_party 分层）

- `app/`：main.c、log.c/h（PRINT_LOG）、ui 任务、sensor_task.c、touch_task.c、usb_host_app.c
- `bsp/`：bsp_uart / bsp_i2c / bsp_ap3216 / bsp_mpu9250 / bsp_sw_i2c / bsp_gt9147 /
  bsp_touch / bsp_sdram / bsp_sdio / bsp_lcd(+text) / lv_font_gbk / lv_port_disp / lv_port_indev
- `third_party/`：FreeRTOS-Kernel / FatFs / tinyusb / lvgl
- `sys_startup/`：自研启动（system_stm32f4xx.c + gcc/startup_stm32f429xx.s + 链接脚本）
- `tools/verify_serial/`：一次性验收脚本（见上）
