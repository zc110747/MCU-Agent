# STM32F429IGT6 USB FS Host (U盘) + FatFs exFAT + FreeRTOS

> STM32F429IGT6 通过 **USB FS Host (TinyUSB)** 读取 U 盘（MSC → SCSI → FatFs），
> 支持 **真正的 exFAT 文件系统**（非 FAT32 伪装），FreeRTOS 堆置于外部 SDRAM。
> 调试串口为 **USART3 (PB10/PB11)**，运行日志经该串口输出。

---

## 1. 硬件规格 / 引脚映射

| 功能 | 外设 | 引脚 | 备注 |
|------|------|------|------|
| 系统时钟 | HSE | 25 MHz 晶振 | PLL: M=25, N=336, P=2 → 168 MHz；Q=7 → 48 MHz USB |
| 调试串口 | USART3 | PB10 (TX) / PB11 (RX) | 115200 8N1，裸机启动即可用 |
| 状态 LED | GPIO | PB1 = LED0（心跳），PB0 = LED1（USB 状态） | 低电平有效 |
| 蜂鸣器 | I2C 扩展 | PCF8574 P0 | 低电平响（已与用户确认） |
| I2C 总线 | I2C2 | PH4 (SCL) / PH5 (SDA) | 100 kHz 标准模式，开漏上拉 |
| I2C 从设备 | — | PCF8574 / AP3216C / MPU9250 / EEPROM | 经 I2C2 挂载 |
| 电容触摸屏 | 软件 I2C（位绑定） | T_SCK = PH6 (SCL) / T_MOSI = PI3 (SDA) | **GT9147/GT911 只有 I2C 接口**，详见 §3.2.3 |
| 触摸复位 | GPIO | T_CS = PI8（→ CT_RST） | 推挽输出 |
| 触摸中断 | EXTI line 7 | T_PEN = PH7（→ CT_INT） | 下降沿，NVIC 优先级 6 |
| 触摸未用脚 | — | T_MISO = PG3 | 电容屏不使用 |
| 网络 PHY | ETH | LAN8720A (RMII) | 复位由 PCF8574 P7 控制 |
| 外部 SDRAM | FMC Bank1 | W9825G6KH-6 (32 MB) | 作为 FreeRTOS 堆；FMC 时钟 = 168/2 = 84 MHz |
| microSD 卡 | SDIO | PC8=D0 PC9=D1 PC10=D2 PC11=D3 PC12=CK PD2=CMD | AF12，4-bit 宽总线，轮询模式（不接 DMA），FatFs 卷 `1:` |
| USB FS Host | OTG_FS | PA11 (DM) / PA12 (DP) | 端口 0，FS 速度，TinyUSB 驱动，FatFs 卷 `0:` |

> SDIO 引脚与本板 FMC 无冲突：FMC 只占用 PC0/PC2/PC3 与
> PD0/PD1/PD8/PD9/PD10/PD14/PD15，PC8–PC12 与 PD2 保持空闲。

---

## 2. 时钟设计（关键约束）

- **SYSCLK = 168 MHz**（不是 180 MHz 上限）。原因：F429 的 180 MHz 下 PLLQ 无法整除得到
  精确的 48 MHz USB 时钟（需 PLLQ=7.5，非法）；168 MHz 配合 **PLLQ=7** 得到干净的
  **48 MHz USB PLL48CLK**，满足 USB 规范。
- **HAL 时基 = TIM7（1 ms）**，中断号为 `TIM7_IRQn = 55`。TIM7 在 F4 上拥有**独立向量**，
  不像 TIM11 那样与 TIM1 TRG/COM 共用 IRQ 线（TIM11 现已完全空闲）。
  TIM7 挂在 APB1：APB1 预分频 /4 → PCLK1 = 42 MHz → 定时器时钟 = 2 × 42 = 84 MHz；
  预分频 83 → 1 MHz 计数时钟，周期 999 → 1 ms 节拍。
- **SysTick 归 FreeRTOS 所有**，驱动 RTOS 调度器。
- **TIM7 与 SysTick 完全独立**：TIM7 提供 `HAL_Delay()` / `HAL_GetTick()` / 外设超时
  （**SDIO 的轮询超时也依赖它**），SysTick 提供 RTOS 节拍，二者互不干扰。

---

## 3. 软件架构

### 3.1 初始化顺序（硬约束：SDRAM 必须先于任何 FreeRTOS 对象）

```
HAL_Init
  → TIM11 时基 (HAL_InitTick)
  → SystemClock_Config (168 MHz / 48 MHz USB)
  → BSP_LED_Init
  → BSP_UART_Init            (裸机即可打印，不依赖 FreeRTOS 堆)
  → BSP_I2C_Init + I2C 总线恢复 + PCF8574 (释放 ETH 复位 / 关闭 BEEP)
  → bsp_sdram_init()         ★ 外部 SDRAM 初始化 + 自测
  → vPortDefineHeapRegions() ★ 此时才把 FreeRTOS 堆注册到 SDRAM
  → usbh_app_init()          (创建 file_task，prio+2；仅建对象，不碰 USB 硬件)
  → xTaskCreate(usbh_host_task, prio+3) / xTaskCreate(led_task, prio+1)
  → vTaskStartScheduler
  (OS 启动后) usbh_host_task 入口:
      → USBH_HW_Init + tusb_init (HOST)   ★ 必须在调度器之后！
      → for(;;) tuh_task()
```

> **为什么必须这样**：`ucHeap[configTOTAL_HEAP_SIZE]`（512 KB）位于 0xC0000000 的外部
> SDRAM。若在 `bsp_sdram_init()` 之前调用 `xTaskCreate` / `pvPortMalloc`，控制块会被写入
> 尚未初始化的内存，破坏堆空闲链表。链接脚本中 `.freertos_heap` 段为 **NOLOAD**，启动代码
> 不会在 FMC/SDRAM 就绪前访问 0xC0000000（否则 HardFault）。

> **USB 初始化必须在 `vTaskStartScheduler()` 之后**（即 `usbh_host_task` 任务体内、`while`
> 循环之前）：`tusb_init()` 会使能 OTG FS 中断，而其中断服务程序（ISR）会调用 FreeRTOS 的
> `xQueueSendToBackFromISR` / `xSemaphoreGiveFromISR`。这些 **FromISR** 系列 API 只有在调度器
> 已经运行（OS 已启动）时才合法。若把 `USBH_HW_Init()` + `tusb_init()` 放在 `main()` 里、
> 调度器启动之前，OTG FS 中断可能在 RTOS 还未就绪时触发，ISR 操作尚未初始化的队列/调度状态，
> 直接破坏系统（表现为系统跑飞、无法进入主循环、串口无后续输出）。因此 USB 硬件与协议栈初始化
> 被刻意推迟到 `usbh_host_task` 任务上下文，确保 OS 已就绪。

### 3.2 任务划分

| 任务 | 优先级 | 职责 |
|------|--------|------|
| `usbh_host_task` | idle+3 | 运行 `tuh_task()`，驱动 TinyUSB 主机协议栈；空闲时阻塞让出 CPU |
| `file_task` | idle+2 | 等待挂载信号量 → `f_mount` → `usb_disk_explore()`：递归遍历 U 盘并打印每个文件内容到 USART |
| `touch_task` | idle+4 | T_PEN 中断 → 二值信号量 → 轮询 GT9147 直到抬手；把坐标发布给 LVGL（见 §3.2.3） |
| `sensor_task` | idle+1 | 每 500 ms 采样 AP3216C（IR/环境光/接近）与 MPU9250（加速度/陀螺/磁力），结果供第 2 页显示 |
| `ui_task` | idle+2 | 启动加载器：LCD/LVGL 初始化 → ASCII 启动页 → 先试 SD 卡字库，失败等 U 盘 → 字库就绪进主界面；每 5 ms 泵 `lv_timer_handler()` |
| `led_task` | idle+1 | LED0 心跳（~500 ms）；LED1 反映 USB 状态（枚举/挂载/错误） |

> `ui_task` 与 `file_task` 同优先级（idle+2），二者均无忙等：存储介质就绪前
> `ui_task` 阻塞在状态轮询 + `vTaskDelay`，`file_task` 阻塞在挂载信号量。LVGL 的渲染泵
> `lv_timer_handler()` 每 5 ms 调用一次，足够驱动刷新且不会饿死其它任务。

### 3.2.1 启动加载流程（新增）

```
ui_task (OS 已启动)
  → lcd_driver_init()                 FMC SRAM + 面板探测，清屏(黑) + 背光开
  → lv_init() + lv_port_disp_init()   draw buffer 经 pvPortMalloc 落在 SDRAM
  → app_ui_show_centered("wait for system start...")     ★ 纯 ASCII，走编译期字表
  → lv_timer_handler()                立即把这一帧推到面板
  → 记录 deadline = 10 s
  → for(;;):
        a. 探测 microSD(SDIO) —— 首次立即探测，之后每 500 ms
           成功 → 打开 1:/SYSTEM/FONT/GBKxx.FON
        b. SD 不可用 → 等 g_usb_state == USB_MOUNTED
           打开 0:/SYSTEM/FONT/GBKxx.FON
        c. 字库就绪(mask != 0) → app_ui_create() 主界面（设备状态三栏面板）
        d. 10 s 到点仍无字库 → app_ui_show_centered("sdcard and usb loader failed!")
           ★ 之后仍每 3 s 静默探测，插入卡/U 盘可热恢复到主界面
        e. lv_timer_handler(); vTaskDelay(5ms);
```

关键设计点：

- **启动页不依赖任何文件**：`"wait for system start..."` 与 `"sdcard and usb loader failed!"`
  全是 ASCII，由 `lv_font_gbk_*` 的**编译期 ASCII 表**渲染（`lv_font_gbk.c` 中
  `letter ∈ [0x20,0x7E]` 走 `ASCII_Fontxx->pTable`），因此字库文件、SD 卡、U 盘都不可用时
  照样能正确显示，不会出现空白或豆腐块。
- **SD 卡优先**：`s_sd_last_try` 在启动页之后被回拨一个重试周期，使**第一次循环**就探测 SD，
  避免 U 盘先枚举完抢跑。
- **10 s deadline 从启动页首帧开始计**，不是从上电开始，面板初始化慢不会吃掉预算。
- **画面切换必须走 `ui_teardown()`**：先 `lv_timer_del()` 删除 1 Hz 刷新定时器，再
  `lv_obj_clean()`。顺序反了会让定时器持有已释放 `lv_obj_t*` 的悬空指针，下一 tick 硬 fault。
- **无卡时不会卡死**：`HAL_SD_Init()` 在无卡时 `SD_PowerON()` 会在**第一个 CMD55 无响应即返回**，
  不会跑满 `SDMMC_MAX_VOLT_TRIAL`(65535) 重试，单次失败耗时 < 1 ms，可以放心轮询重试。

### 3.2.2 microSD（SDIO）+ FatFs 双卷

| 项 | 值 |
|----|----|
| 总线 | SDIO 4-bit，AF12，上拉 |
| 时钟 | SDIOCLK = 48 MHz（PLLQ=7，与 USB 同源）；`ClockDiv=2` → 卡时钟 48/(2+2) = **12 MHz** |
| 传输方式 | **轮询**（`HAL_SD_ReadBlocks/WriteBlocks`），不接 DMA |
| FatFs 卷 | `0:` = USB MSC（TinyUSB），`1:` = microSD（SDIO）；`FF_VOLUMES = 2` |

- **不用 DMA 的原因**：DMA2 与板上其它外设共用，而 SD 卡只用于按扇区取字模，轮询路径的代价
  可以忽略，却彻底消除了 cache/DMA/IRQ 冲突这一类问题，也无需任何 FromISR 适配。
- **4 字节对齐**：SDIO FIFO 在 HAL 内以 `uint32_t*` 访问，目标缓冲区必须 4 字节对齐。
  FatFs 多数时候传的是 `FIL.buf`（已对齐），但 `f_read()` 存在直通用户缓冲区的快路径，
  因此 `fs_diskio.c` 中对未对齐地址统一经 `s_sd_scratch[512]` 中转。
- **统一 diskio 胶水**：`app/fs_diskio.c` 同时实现 `0:`（USB MSC）与 `1:`（SDIO）的
  `disk_status/initialize/read/write/ioctl`，并用一把 `fs_lock()` 互斥量串行化所有 FatFs 入口
  （USB MSC 传输只有一个全局 busy 标志，两个任务并发会造成丢唤醒死锁）。

### 3.2.3 电容触摸链路（GT9147 / GT911 + 软件 I2C + T_PEN 中断）

**关于"模拟 SPI"的说明（重要）**

板子丝印把触摸排针标成了 SPI 口（`T_CS / T_SCK / T_MOSI / T_MISO / T_PEN`），
但焊在屏上的是 **GT9147**，而 **GT9147/GT911 数据手册里只有 I2C 接口，没有 SPI 模式**。
这块板上的两根"SPI"数据线实际就是触控芯片的 I2C：

| 排针名 | MCU 引脚 | 触控芯片信号 |
|--------|----------|--------------|
| `T_SCK` | PH6 | `CT_SCL`（串行时钟） |
| `T_MOSI` | PI3 | `CT_SDA`（串行数据，双向） |
| `T_CS` | PI8 | `CT_RST`（复位） |
| `T_PEN` | PH7 | `CT_INT`（中断 / 触摸指示） |
| `T_MISO` | PG3 | 电容屏不使用 |

因此 `bsp/bsp_sw_i2c.c` 实现的是**软件位绑定 I2C 主机**（开漏 + 上拉，约 165 kHz，
时序用 DWT 周期计数忙等），而不是软件 SPI。

**信号链**

```
手指 ──> GT9147 INT (T_PEN = PH7) 下降沿
          └─> EXTI line 7（SYSCFG 复用到 GPIOH，IMR/FTSR bit7，NVIC 优先级 6）
                └─> HAL_EXTI_IRQHandler -> bsp_touch 回调（ISR 上下文）
                      └─> xSemaphoreGiveFromISR(二值信号量)
                            └─> touch_task 被唤醒 -> bsp_touch_scan()
                                  └─> 软件 I2C 读 0x814E 状态 + 0x8150 坐标
                                        └─> 写回 0x814E=0 允许下一次中断
                                              └─> 发布 (x, y, pressed) 给 LVGL indev
```

**为什么中断之后还要轮询**：部分 GT9xx 模组每次接触只在 INT 上打一个脉冲，
有些则在整个接触期间持续打脉冲。所以中断只当作"唤醒信号"，任务随后以 15 ms
周期轮询，直到连续 3 次读到"无触点"才算抬手。这样按下/移动/抬起在两种模组上都正确。
`bsp_touch_wait()` 的 1 s 超时是兜底：即使 INT 线没焊上，触摸仍能以轮询方式工作
（此时日志会打出 `WARNING: contact seen without T_PEN interrupt`）。

**初始化与 ID 校验**（`bsp/bsp_gt9147.c`）：

1. RST(PH8/PI8) 拉低 10 ms → 拉高 10 ms → INT 置浮空输入（芯片在复位释放瞬间
   采样 INT 来锁存 I2C 地址：INT 高 → `0x14`，低 → `0x5D`）
2. 依次尝试 `0x14` / `0x5D`，读 `0x8140` 的 4 字节 Product ID
3. **打印 ID 并判定是否匹配**（`911 / 9147 / 1158 / 9271 / 928`）
4. 读 `0x8047..0x804B` 得到配置版本与**芯片自身触摸分辨率**，用于坐标缩放
5. 仅当 ID 恰好是 `9147` 且版本 < `0x60` 时才上传 184 字节配置块（配置块是
   9147 专用的，不能给 GT911 用）

**坐标映射**（`bsp/bsp_touch.c`）：芯片自报分辨率 480×800，与 LVGL 画布
（= LCD 驱动的 GRAM 窗口）480×800 完全一致，因此**默认是恒等映射**。
`TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` 三个宏是换屏时唯一需要改的地方。
每次触摸都会打印 `raw=(x,y) -> lv=(x,y)`，一次点击即可确定正确的组合。

### 3.2.4 UI 四页面与底部导航

| 页 | 标题 | 内容 |
|----|------|------|
| 0 | 状态 | 系统初始化（LCD ID / SD 卡 / USB）/ 运行信息（USB 状态、字库、主频、运行时间、字形缓存）/ 故障·消息 |
| 1 | 硬件信息 | AP3216C（红外 / 环境光 / 接近）+ MPU9250（加速度 / 角速度 / 磁场）+ 采样统计 |
| 2 | 控制 | 两个大按键：`LED`（切 PB0，低电平点亮）/ `蜂鸣器`（切 PCF8574 P0，低电平响）；状态栏显示 LED / 蜂鸣器实时状态 |
| 3 | 时钟 | 内部 RTC 实时钟：**时钟区占约 80% 原高度（两行 gbk_32）** `年-月-日` / `时:分:秒`（2 Hz 刷新）；**下方合并设置区**：时间 5 字段（年/月/日/时/分）+ 闹钟 2 字段（时/分）点选高亮；**底部两行按键**：上行 `上`(↑ 循环)/`下`(↓ 循环，箭头 chevron 非文字)/`设置`(整体写 RTC+启用闹钟)，下行 `闹钟开启`/`闹钟关闭`；`切换` 按钮已移除（改为点字段选中）；状态标签显示 `闹钟 HH:MM 开/关`，**仅在提交时刷新**（点击 `设置`/`闹钟开启`/`闹钟关闭` 才更新，上/下编辑不改动标签）；**上/下键均支持循环**（到最大再点回最小、到最小再点回最大）；闹钟**持久化到 AT24C02 EEPROM**（主存，初始化 `BSP_RTC_Alarm_LoadFromEEPROM()` 读入并同步写 RTC Alarm 备份寄存器），开启/关闭同步改 RTC 状态+设置+EEPROM（变更检测：未变不重复写）；**闹钟关闭同时关闭蜂鸣器**；到点由 2 Hz tick 软件比对驱动蜂鸣器响铃，**响铃后 60 s 自动关闭**（停止蜂鸣器 + 持久化 off + 刷新标签）；页面代码已拆分到 `app/ui/`（`page_status.c`/`page_hwinfo.c`/`page_ctrl.c`/`page_rtc.c`，框架在 `app_ui.c` + `app/ui/ui_common.h`） |

底部导航条四页共用：左 / 右两个**图标按钮**（LVGL `lv_line` 画的 V 形箭头，
不依赖字库文件）+ 中间页码 `x / 4`。点左/右以 `±1` 循环翻页（`app_ui_switch_page()`）。

- **PB0 由 UI 独占**：`main.c` 的 `led_task` 原先用 `LED1(PB0)` 做 USB 状态指示，会与
  控制页的手动开关互相覆盖；现 `led_task` 只保留 `LED0(PB1)` 心跳，`PB0` 完全交给控制页。

- **几何全部自适应**：布局从 `lv_disp_get_hor_res/ver_res()` 取，不再硬编码 800×480。
  因为本工程的 GRAM 窗口是 **480×800**（见 §3.4），之前硬编码的 800×480 会被横向裁掉
  320 px、纵向空出 320 px；改成自适应后内容始终铺满。
- **切页不删定时器**：切页用 `ui_rebuild()`（只 `lv_obj_clean()` 再重建控件），
  刷新定时器保持存活；只有切到居中消息页才走 `ui_teardown()`（先删定时器再清屏）。
  定时器回调对所有句柄做了 NULL 判断，重建期间不会解引用悬空指针。

### 3.5 传感器采样（`app/sensor_task.c`）

AP3216C（`0x1E`）与 MPU9250（`0x68`，内含 AK8963 `0x0C`）都挂 I2C2，每 500 ms 采样一轮，
结果通过 `sensor_get()` 拷贝给 UI 线程（临界区保护）。

- 采样在自己的任务里做：MPU9250 一轮约 14 次 I2C 事务、耗时 ~15 ms，放进渲染泵会卡住重绘。
- **磁力计是可选的**：`AK8963_WIA`(0x00) 必须读到 `0x48` 才算存在。**本板实测 WIA=0x00**
  —— 这颗"MPU9250"没有可用磁力计。此时 init 返回 `-3`，加速度/陀螺照常采样，
  页面显示「磁场  AK8963 未装配」而**不是**谎报 0.0 uT（旧代码正是把全零当成功上报）。
- **失败退避 + 限流打印**：失败后隔 2 s 重试；同一设备的错误最多每 60 s 打印一次
  （UART TX 环形缓冲满会静默丢字节，见 §6.1）。

### 3.5.1 ⚠️ I2C2 在 FreeRTOS 起来后失败的根因与防护（2026-08-29 实测定位）

**症状**：传感器 init 全部成功，但触摸中断使能之后
`[SENS ] ... read FAILED (rc=-1)`，且**之后每次都失败**。

**根因链**

1. **T_PEN (PH7) 中断风暴** —— PH7 原先是浮空输入，且紧邻 PH6（位绑定 SCL，165 kHz）。
   实测 **46 925 次/秒**（`irq` 计数在数秒内冲到 93 014）。
2. `HAL_I2C_Mem_Read/Write` 是**轮询式**传输，CPU 必须留在循环里逐字节搬运；
   而触摸任务优先级（idle+4）高于传感器任务（idle+1），传输会被位绑定扫描抢占。
3. 抢占 → HAL 10 ms 超时 → **从机仍拉住 SDA、I2C2 锁在 BUSY**。
4. 旧代码失败后只退避重试，**从不调 `BSP_I2C_Recover()`** → 总线永久卡死。

**三层防护（缺一不可）**

| 层 | 措施 | 解决什么 |
|----|------|----------|
| 源头 | PH7 改 `GPIO_PULLUP`（仅地址锁存那一瞬用 NOPULL） | 浮空引脚拾噪 |
| 源头 | 位绑定 I2C 事务期间屏蔽 EXTI line 7 | 自身 SCL 串扰 |
| 源头 | **ISR 内立即屏蔽 line 7**；任务侧消抖 5 ms 后 `bsp_touch_irq_rearm()` 重新武装 | ISR 速率 ~47 kHz → ~20 Hz |
| 隔离 | 传感器读取用 `vTaskSuspendAll()`/`xTaskResumeAll()` 包成**原子操作**（中断仍开） | 传输不被抢占 |
| 容错 | I2C 超时 10 ms → **50 ms**；失败先 `BSP_I2C_Recover()` 再**立即重试一次** | 单次抖动不成故障；总线自愈 |
| 兜底 | 触摸轮询循环上限 `TOUCH_MAX_POLLS`(3 s)，幻接触不能永久占用任务 | 不死循环 |
| 自诊断 | IRQ 速率看门狗，超 1000/s 打一条 WARNING | 一眼看出风暴 |

**验证**：SWD 直读 `s_data`（`tools/verify_serial/verify_sensors.py`，**7/7 PASS**）
—— `errors=0`、`samples=38` 正常推进、`|a| = 1.00 g`（板子平放，重力全在 Z 轴）。

### 3.6 日志系统：PRINT_LOG 全局可控（替换全部 printf）

**约定：工程内所有应用日志一律用 `PRINT_LOG(...)`，不再直接调用 `printf()`。**
`snprintf()` 属于字符串格式化，不属于日志，**保留不动**。

```
PRINT_LOG(fmt, ...)                 app/log.h   -> 开关关闭时展开为 ((void)0)
   └─> printf_log()                 app/log.c   -> va_list 转发
         └─> vsnprintf() 栈缓冲(192 B)          -> 定长，不分配堆
               └─> uart_write()     bsp/bsp_uart.c
                     ├─ 调度器未运行 -> HAL_UART_Transmit() 阻塞轮询（启动横幅不丢字节）
                     └─ 调度器已运行 -> 互斥量 + 临界区入环，由 TXE ISR 排空
```

**为什么要换掉 printf**

| 问题 | printf | PRINT_LOG |
|------|--------|-----------|
| 全局开关 | 无，只能逐处注释 | `PRINT_LOG_ENABLE=0` 一处关掉，编译成空语句 |
| 走 newlib 路径 | 是（会重入 `_write` → `uart_write`） | 否，直接调 `uart_write` 并带显式长度 |
| 堆依赖 | 可能分配 | 无（192 B 栈缓冲），堆损坏/未初始化时仍可用 |
| 启动阶段 | 依赖 `syscalls _write` | 调度器未启动自动切阻塞轮询，横幅不丢 |
| 多任务串行化 | 无 | 互斥量 + 整行临界区入环，日志行不交错 |

**开关方式**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_PRINT_LOG=OFF -S .
```

CMake 会打印 `-- PRINT_LOG: compiled out (PRINT_LOG_ENABLE=0)`。
`app/log.h` 里也有 `#ifndef PRINT_LOG_ENABLE` 兜底默认值为 1。

**两条必须遵守的规则**

1. **`PRINT_LOG` 不能在 ISR 里调用** —— 里面会拿互斥量。中断上下文请用
   `uart_write()` / `uart_puts()`。
2. **U 盘内容 dump 不走 `PRINT_LOG`** —— 它是二进制原文且可能不含 NUL，不能直接当格式串
   （文件内容里出现 `%s` 会被当成格式化指令直接崩）。它走 `uart_write()` 直连，
   并单独用 `#if PRINT_LOG_ENABLE` 包住，所以关日志时连盘都不会读。

**已知限制**

- `PRINT_LOG` 与 newlib-nano 一样**不支持 `%f`**（链接参数没有 `-u _printf_float`）。
  需要小数请手工拆成整数 + `%02d`（见 `app/app_ui.c` 的 `fmt_fixed2()`）。
- 单行上限 192 B，超长会被 `vsnprintf` 截断（当前最长日志行约 110 B）。
- `syscalls.c` 的 `_write` 仍然保留，作为第三方库 / 残留 `printf` 的兜底通道。

### 3.4 UI 线程（FMC/8080 LCD + LVGL + GBK 字库）

在 **800×480 正点原子 LCD（NT35510 / ILI9806E，FMC Bank1 NE1，16-bit 8080 接口）**
上，用 **LVGL v8** 渲染一块极简深色信息面板（无色彩点缀，统一浅色文字）。字库放在
**`SYSTEM/FONT/`**（GBK 点阵 `GBK12/16/24/32.FON` + `UNIGBK.BIN`），不烧进 Flash；
**优先从 microSD `1:` 读取，读不到再等 U 盘 `0:`**（详见 §3.2.1）。

#### 硬件接口（LCD，FMC Bank1 NE1，8080 16-bit）

| 信号 | 引脚 | 备注 |
|------|------|------|
| 数据总线 | PD0–15 / PE7–15 | FMC D0–D15（与 SDRAM 共享 FMC，重配置为 AF12 幂等安全） |
| 地址/控制 | PF0–5/12–15、PG0–5、PD11–15 | FMC A0–A18（RS=A18 → `LCD_BASE=0x60000000\|0x0007FFFE`） |
| 背光 | PB5 | 高有效，初始化末打开；低电平有效 LED 同板不冲突 |
| NE1 片选 | FMC_NE1 | Bank1、16-bit、`ExtendedMode=ENABLE`、写时序收紧 |

#### 启动流程（`app/ui_task.c`）

见 **§3.2.1**（启动加载流程）。本节的 LVGL 相关要点：

- `lv_port_disp_init()` 用 `pvPortMalloc()` 在 **SDRAM** 分配 draw buffer
  （`800 × 60 × 2B ≈ 96 KB`），不占内部 192 KB SRAM。
- `LV_TICK_CUSTOM = 1` 绑定到 `HAL_GetTick()`，即 **TIM7**；`ui_task` 里无需 `lv_tick_inc()`。
- 两种画面互斥（详见 §3.2.1）：
  1. `app_ui_show_centered()` — 纯 ASCII 居中页（启动页 / 超时失败页）
  2. `app_ui_create()` — 中文设备状态三栏面板（系统初始化 / 运行信息 / 故障·消息）

#### 关键设计点

- **LVGL 内存放 SDRAM**：`lv_conf.h` 设 `LV_MEM_ADR=0xC0100000U`、`LV_MEM_SIZE=256KB`，
  位于 FreeRTOS 堆（`ucHeap` @0xC0000000~0xC007FFFF）之后，避免 256 KB 静态 BSS 数组塞爆
  内部 192 KB SRAM（早期一版误把 `LV_MEM_ADR` 重新定义成 0，会导致链接失败/内存越界，已修）。
- **draw buffer 放 SDRAM**：`lv_port_disp.c` 中 `pvPortMalloc(800×60×2B≈96KB)`，不占内部 SRAM；
  `disp_flush` 经 `lcd_color_fill()` 把渲染区整块写入面板。
- **GBK 字库桥**：`lv_font_gbk.c` 的 `get_glyph_dsc/get_glyph_bitmap` 回调 —— ASCII 用编译期
  表（来自 `lcd_ascii_font.c`），中文走编译期 `lv_gbk_map.c` 的 Unicode→GBK 映射，再从 U 盘
  文件读原始 MSB/列扫字模、单遍转 LVGL 连续行位流；`lcd_driver_get_hzmat_raw()` 提供免重排原始数据。
- **字库路径参数化**：`lcd_driver_font_init(const char *vol)` 以卷前缀 `1:`（microSD）或 `0:`
  （USB）拼接 `<vol>/SYSTEM/FONT/GBKxx.FON`，由加载器决定来源；`lcd_driver_font_source()`
  回报当前来源，主界面据此显示「字库 SD 卡 / U 盘 …已就绪」。切换来源前会 `f_close()` 旧文件并
  `lv_font_gbk_reset_cache()`，避免残留旧卷的字形缓存。
- **零警告**：第三方 LVGL 源码经 `set_source_files_properties(... COMPILE_OPTIONS "-w")` 静默，
  工程自有代码保持 `-Wall -Wextra` 严格零警告。

### 3.3 USB 主机数据流

```
U 盘插入
  → OTG_FS_IRQHandler → tuh_int_handler(0)
  → tuh_task() (usbh_host_task) 枚举设备
  → tuh_msc_mount_cb()  (在 usbh_host_task 上下文内)
        → xSemaphoreGive(xUsbMountSem)   ★ 普通 Give（非 FromISR，因为回调在任务上下文）
  → file_task 的 xSemaphoreTake 解除阻塞
  → f_mount / f_open / f_write / f_read
        → FatFs diskio 胶水 (app/usb_host_app.c)
              disk_read/write  → tuh_msc_read10/write10
              wait_for_disk_io 阻塞直到完成回调 (disk_io_complete) 清 busy 标志
```

> 设计要点：`disk_read/write` 提交 SCSI 命令后**阻塞**等待完成回调（在 `tuh_task` 内触发），
> 因此 `usbh_host_task` 与 `file_task` 必须并发运行——这正是上述双任务结构的由来。

---

## 4. exFAT 设计（真正支持，非 FAT32 伪装）

- 采用 **ChaN FatFs R0.15**，设置 `FF_FS_EXFAT=1`（见 `third_party/FatFs/ffconf.h`）。
- 同时开启 `FF_USE_MKFS=1`、`FF_USE_LFN=1`、`FF_FS_NORTC=1`（免 `get_fattime()`）。
- 格式化参数（`app/usb_host_app.c` 的 `format_exfat()`，默认关闭，避免误清用户数据）：
  - `fmt = FM_EXFAT`（真正的 exFAT）
  - `align = 256 sectors`（= 128 KB）— FAT / 数据区对齐
  - `au_size = 128 KB` — 分配单元（簇）大小
- **exFAT 规范要求对齐的是「DATA REGION（簇堆）」，不是 FAT**。本工程二者均满足 128 KB。
- 真实性证明见 `verify_exfat/harness.c`（PC 端 FatFs 验证工具）：用**与固件完全相同的
  FatFs R0.15 + ffconf.h** 对 RAM 盘做 `f_mkfs(FM_EXFAT, …)`，再解析原始卷证明
  VBR 引导签名、簇堆绝对 LBA 128 KB 对齐、分配单元 128 KB。`make` 后运行 `harness.exe`
  得到 `12 passed, 0 failed — VERDICT: PASS`。

---

## 5. 构建（CMake + Ninja + arm-none-eabi-gcc）

### 5.1 工具链（通过环境变量提供，不写死本机绝对路径）

工程内所有构建/调试脚本（`CMakeLists.txt`、`openocd/stm32f429_stlink.cfg`、`.vscode/*`）
均通过**环境变量**解析工具链路径，换机器只需改环境变量、不改工程文件。推荐设置：

| 环境变量 | 含义 | 本机示例 |
|----------|------|----------|
| `ARM_GNU_TOOLCHAIN_BIN` | `arm-none-eabi-gcc`/`gdb`/`nm`/... 所在目录 | `E:/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi/bin` |
| `OPENOCD_BIN` | `openocd.exe` 所在目录 | `D:/software/ST/OpenOCD/bin` |
| `OPENOCD_SCRIPTS` | OpenOCD `scripts` 目录（`[find]` 基准） | `D:/software/ST/OpenOCD/share/openocd/scripts` |
| `CMAKE_BIN` | `cmake.exe` 所在目录 | `C:/Software/msys2/mingw64/bin` |
| `NINJA_BIN` | `ninja.exe` 所在目录 | `C:/Software/msys2/mingw64/bin` |

也可把工具加入系统 `PATH`，并将上述变量设为空字符串（VSCode 配置统一用 `${env:VAR}` 语法）。
版本：`arm-none-eabi-gcc` 15.3.1；CMake 4.2.1；Ninja 1.13.2；PC 端 `gcc`（仅 exFAT 验证）15.2.0。

### 5.2 命令

```bash
# Release（默认，日志开）
cmake -B build -DCMAKE_BUILD_TYPE=Release -S .
cmake --build build --target stm32f429_tinyusb_ui.elf

# Debug
cmake -B build_dbg -DCMAKE_BUILD_TYPE=Debug -S .
cmake --build build_dbg --target stm32f429_tinyusb_ui.elf

# 关闭全部应用日志（PRINT_LOG_ENABLE=0）
cmake -B build_nolog -DCMAKE_BUILD_TYPE=Release -DENABLE_PRINT_LOG=OFF -S .
cmake --build build_nolog --target stm32f429_tinyusb_ui.elf
```

- `ENABLE_PRINT_LOG`（CMake option，默认 ON）→ 产出 `-DPRINT_LOG_ENABLE=0|1`。
  关掉后所有 `PRINT_LOG(...)` 编译成空语句，FLASH 省约 6 KB、串口零流量（见 §3.6）。

- 编译标志：`-Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fdata-sections -ffunction-sections -g`；
  Debug 用 `-Og`，Release 用 `-Os`。
- 链接：`--gc-sections --no-warn-rwx-segments --print-memory-usage -specs=nano.specs`。
- 后置步骤：生成 `.hex` / `.bin` / `.map`。
- **零警告**为硬性要求（已通过双构建验证）。第三方 **LVGL** 源码经
  `set_source_files_properties(... COMPILE_OPTIONS "-w")` 静默，工程自有代码保持
  `-Wall -Wextra` 严格零警告。

### 5.3 产物（双构建，零警告）

> 下表为接入 **PRINT_LOG 日志系统（替换全部 printf）** 后的最新占用。相比上一版
> （触摸 + 传感器 + 双页面）FLASH **-4.1 KB**：`PRINT_LOG` 走 `vsnprintf` + `uart_write`
> 的定长路径，绕开了 newlib `printf` 的完整格式化栈，代码反而更小。

| 构建 | 日志 | FLASH | RAM (内部) | SDRAM 堆 | 警告 |
|------|------|-------|-----------|----------|------|
| Release | 开 | 298,120 B / 1 MB (28.43%) | 30,272 B / 192 KB (15.40%) | 512 KB / 32 MB (1.56%) | 0 |
| Debug   | 开 | 259,236 B / 1 MB (24.72%) | 30,264 B / 192 KB (15.39%) | 512 KB / 32 MB (1.56%) | 0 |
| Release | **关** | **291,960 B / 1 MB (27.84%)** | 30,272 B / 192 KB (15.40%) | 512 KB / 32 MB (1.56%) | 0 |

> 说明：本工程 `-Og`(Debug) 产出的代码比 `-Os`(Release) 更小（与工具链版本相关的既有现象，
> 上一版同样如此），三种配置均**零警告**。日志关掉后省下 **6,160 B** FLASH
> （全部格式串 + 调用点被 `--gc-sections` 回收），串口**零字节**输出（§3.6 已用 SWD 实测）。

### 5.4 exFAT 验证工具

```bash
cd verify_exfat
gcc harness.c ../third_party/FatFs/ff.c ../third_party/FatFs/ffsystem.c \
    ../third_party/FatFs/ffunicode.c -I../third_party/FatFs -o harness.exe
./harness.exe
```

---

## 6. 验收结论（摘要）

| 项目 | 结果 | 说明 |
|------|------|------|
| 双构建零警告（Debug/Release） | ✅ PASS | 见 §5.3 |
| exFAT 真实性 + 128 KB 数据区对齐 + 128 KB 簇 | ✅ PASS | 见 §4 / `verify_exfat` |
| TinyUSB USB FS Host 集成 | ✅ 编译验证通过 | 枚举/挂载回调与磁盘胶水已贯通 |
| SDRAM 在 FreeRTOS 之前初始化 | ✅ 代码审查通过 | 见 §3.1 顺序约束 |
| TIM11 / SysTick 独立时基 | ✅ 代码审查通过 | 见 §2 |
| USART3 调试输出 | ✅ 硬件验证通过 | COM5 实测完整启动横幅 + U 盘内容回显（§7.5） |
| **U 盘真实枚举 + 目录递归遍历 + 文件内容串口打印** | ✅ 硬件验证通过 | 2026-08-26 COM5 实测：枚举→挂载→遍历(含子目录)→逐文件正文打印全链路 PASS（§7.5） |
| **SDRAM/FMC 运行时时序** | ✅ 硬件验证通过 | 自测通过；`Heap object @ 0xC0002F38` 证实堆在 SDRAM（§7.5） |
| USB 初始化时序（OS 启动后） | ✅ 硬件验证通过 | 修正中断触发 FromISR queue 破坏 RTOS 的问题（§3.1） |
| LCD 驱动（FMC Bank1 NE1 8080 16-bit）集成 | ✅ 编译验证通过 | `bsp_lcd.c` 内联 F429 FMC GPIO 配置 + NT35510/ILI9806E 初始化序列（§3.4） |
| LVGL v8 显示端口 + SDRAM 内存布局 | ✅ 编译验证通过 | `LV_MEM_ADR=0xC0100000U`/`256KB` 避开 FreeRTOS 堆；draw buffer 经 `pvPortMalloc` 落 SDRAM（§3.4） |
| GBK 字库桥（U 盘 `0:/SYSTEM/FONT/`） | ✅ 硬件验证通过 | `lv_font_gbk` 回调 + `lcd_driver_font_init()` 打开 GBKxx.FON（§3.4）；**UNIGBK.BIN 非渲染必需**（Unicode→GBK 走编译期表 `lv_gbk_map.c`） |
| UI 线程端到端（LCD 探测 ID → 挂载 → LVGL 渲染面板） | ✅ 硬件验证通过 | 2026-08-27 COM5 实测：`[LCD ] controller ID = 0x8000` → `[FONT] font status mask = 0x1E` → 中文面板渲染，无卡死 |
| **FatFs 并发死锁修复** | ✅ 硬件验证通过 | `file_task`(explore) 与 `ui_task`(字体挂载/渲染) 并发访问同一 U 盘 → 共享 `_disk_busy` 丢唤醒死锁；已用互斥量串行化所有 FatFs 入口（见下「已知问题」） |
| **HAL 时基 TIM11 → TIM7** | ✅ 硬件验证通过 | 2026-08-28 COM5 实测：`HAL_Delay()`/SDIO 轮询超时均正常，TIM7_IRQn=55 独立向量 |
| **microSD(SDIO 4bit) 驱动** | ✅ 硬件验证通过 | 2026-08-28 COM5 实测：无卡时 `[SD  ] SDIO init FAILED (no card?)` 优雅降级，单次失败 <1 ms，不卡死 |
| **启动加载器 SD→USB 优先序** | ✅ 硬件验证通过 | 实测日志顺序：启动页 → `[SD  ] SDIO init FAILED` → USB 挂载 → `[FONT] source=0:` → 主界面 |
| **10 s 超时失败页** | ✅ 硬件验证通过 | 关闭 USB 回退（`-DLOADER_ENABLE_USB_FALLBACK=0`）复现「两端都无介质」，10 s 后居中显示 `sdcard and usb loader failed!`，**只打印一次** |
| **CJK 字形确实从字库渲染** | ✅ 硬件验证通过 | `[UI  ] glyph cache: hits=... misses=45` —— miss>0 证明中文字形真的从 `GBKxx.FON` 读出 |
| **启动流程自动化验收** | ✅ PASS 17/17 | `tools/verify_serial/verify_boot_flow.py`（烧录 + COM5 抓 25 s + pass/fail 计数） |
| **GT9147/GT911 软件 I2C 识别 + ID 校验** | ✅ 硬件验证通过 | 2026-08-28 COM5 实测：`product ID = "911" (addr 0x14) -> MATCH`，自报分辨率 480×800 |
| **T_PEN 中断链路（EXTI→ISR→信号量→任务）** | ✅ PASS 7/7 | `verify_touch_irq.py` 经 `EXTI_SWIER` 软注入 line 7，任务被唤醒（§7.7.1） |
| **AP3216C + MPU9250 采样** | ✅ 硬件验证通过 | 2026-08-28 COM5 实测：`AP3216C init OK` / `MPU9250 init OK (WHO_AM_I check)` |
| **UI 四页面 + 底部左右图标按钮** | ✅ 编译 + 代码审查通过 | 布局自适应 480×800 画布；翻页循环(0→1→2→3→0)；图标用 `lv_line` 画，不依赖字库文件 |
| **RTC 时钟页（内部 RTC 走时 + 时间/闹钟设置写回）** | ✅ 编译 + 烧录 + SWD 寄存器验证 | Release/Debug 零警告；OpenOCD 校验（Release 310084 B / 29.57%、Debug 269716 B / 25.72%）；布局按反馈重排：**时钟区缩至 80% 原高度**、时间+闹钟合并为一区、上/下改箭头 chevron、`切换` 移除；**底部两行按键**：上行 `上`/`下`/`设置`，下行 `闹钟开启`/`闹钟关闭`；**上/下键均循环**（到极值回绕）；新增 `bsp_rtc` 闹钟 **AT24C02 EEPROM 持久化** API（`BSP_RTC_Alarm_LoadFromEEPROM` 初始化读入并写 RTC Alarm 备份寄存器、`BSP_RTC_Alarm_Persist` 变更检测避免重复写）；SWD 读 RTC `TR` 0x2→0x5（3 s 走时），EEPROM→BKP 镜像一致、`Persist(7,30,1)` 跨复位存活（`verify_alarm_eeprom.py` **PASS 6/6**）；**修复 HAL RTC 备份寄存器索引语义坑**（`HAL_RTCEx_BKUPWrite` 的 `BackupRegister` 是索引 0..19，须用 `RTC_BKP_DRx` 而非 `RTC_BKPxR`=0xFFFFFFFFUL 位掩码，否则写到垃圾地址、备份永不更新；首上电标志 `RTC_BKP0R` 一并修正）；**UI 重构为 `app/ui/` 每页独立文件**（`page_status/page_hwinfo/page_ctrl/page_rtc` + 框架 `app_ui.c`/`ui_common.h`，零警告双构）；本次新增行为（逻辑编译通过 + SWD 启动/走时已验证，点屏交互待人工确认）：**闹钟标签仅提交时刷新**（设置/开启/关闭才更新，上/下编辑不改）、**闹钟关闭同时关蜂鸣器**、**响铃 60 s 自动关闭**（停蜂鸣器+持久化 off+刷新标签） |
| **控制页（LED PB0 / 蜂鸣器 PCF8574 P0 切换 + 状态显示）** | ✅ 编译 + 烧录 + SWD 启动验证 | Release/Debug 零警告；OpenOCD 校验 300516 B；SWD 读 `s_uptime_sec` 4→9 证明 UI tick 运行；LED/蜂鸣器电气动作需手指点屏确认 |
| **手指触摸坐标上报 / 实际翻页** | ⏳ 待人工 | 需手指点屏确认，脚本 `verify_touch.py` 已就绪（§7.7.2） |
| **PRINT_LOG 替换全部 printf + 全局开关** | ✅ **PASS 6/6** | 三种配置零警告；SWD 实测开关行为（§7.8） |
| **I2C2 中断风暴根因修复 + 三层防护** | ✅ **PASS 7/7** | 传感器 `errors=0`、`\|a\|=1.00 g`；SWD 直读 `s_data`（§7.9） |
| **启动流程回归（修复后）** | ✅ **PASS 17/17** | `verify_boot_flow.py` 无退化 |

> 详细证据与「编译验证 vs 硬件验证」标签见 **`ACCEPTANCE_REPORT.md`**。

### 6.1 已知问题与修复

**FatFs 并发死锁（字体挂载阶段卡死，2026-08-27 已修复）**
- 现象：启动到 `[FONT] mounting U-disk fonts` 后卡死，中文字库无法加载（`mask` 永不打印）。
- 根因：`file_task` 与 `ui_task` 同为 `idle+2` 优先级，二者在 `g_usb_state=USB_MOUNTED` 后**同时对同一 U 盘做 FatFs I/O**（前者 `usb_disk_explore` 递归遍历+dump，后者 `lcd_driver_font_init` 二次 `f_mount`+`f_open`，且渲染期 `font_read_raw` 持续读字模）。底层 `disk_read/disk_write` 用单个全局 `_disk_busy[0]` 标志 + `wait_for_disk_io` 自旋等 MSC 完成回调，并发时 **完成回调清错标志 → 丢唤醒 → 一方永久自旋**，盘 I/O 整体死锁。
- 修复：`usb_host_app.c` 新增 FreeRTOS 互斥量 `xFsLock`，`fs_lock()/fs_unlock()` 串行化所有 FatFs 入口；`file_task` 的 `f_mount`+`usb_disk_explore` 整段加锁；`lcd_driver_font_init` 去除多余 `f_mount`（盘已由 file_task 挂载，二次挂载会换掉 FATFS 工作区指针）、仅 `fs_lock` 包住 `f_open`；`font_read_raw` 的 `f_lseek+f_read` 也加 `fs_lock`。
- 验证：COM5 实测 `[FONT] font status mask = 0x1E`（GBK12/16/24/32 全开），无卡死，中文面板正常渲染。

**UART TX 环形缓冲溢出导致日志被静默丢弃（2026-08-28 已修复）**
- 现象：新增的 `[SD  ]` / `[FONT]` / `[UI  ]` 日志时有时无，且已打印的行出现字节缺失
  （如 `Heap [FONT] source=0:`）。
- 根因两处叠加：
  1. `uart_write()` 在环形缓冲满时**直接 `break` 丢弃**字节，而 `UART_TX_BUF_SIZE` 只有 **512 B**，
     115200 Baud 下任一任务的连续打印即可撑爆；
  2. `file_task` 对 U 盘做**全量内容 dump**，本次盘中含 3 MB 的 `GBK32.FON` 与若干 JPG，
     按 115200 Baud 需要数分钟才能吐完，期间环形缓冲长期满载 → 其它任务所有日志被吞掉。
- 修复：`UART_TX_BUF_SIZE` 512 → **2048**；dump 加两道闸（`app/usb_host_app.c`）——
  超过 `DISK_MAX_DUMP_BYTES`(2048 B) 的文件**只列目录项不 dump 内容**，且整轮遍历内容总量
  受 `DISK_DUMP_BUDGET`(16 KB) 限制。`[DIR]/[FILE]` 列表本身不截断，既有验证脚本不受影响。

**SD 空卡槽重试刷屏（2026-08-28 已修复）**
- 现象：`[SD  ] SDIO init FAILED (no card?)` 每 500 ms 重复打印。
- 修复：`sd_card_set_quiet()` —— 首次失败后静音；10 s deadline 之后探测降频到 3 s 一次。
  热插拔恢复能力保留，只是不再刷屏。

**超时失败页重复触发（2026-08-28 已修复）**
- 现象：`[UI  ] timeout: ...` 每 5 ms 打印一次。
- 根因：deadline 分支判断写成 `s_state != LOAD_OK`，而 `LOAD_FAILED` 同样满足该条件。
- 修复：改为 `s_state == LOAD_BOOT`，只在真正从启动页超时时触发一次。

**触摸坐标被钳到 (0,0)：画布尺寸取到了 0（2026-08-28 已修复）**
- 现象：`[TOUCH] ready: ... canvas 1x1`，所有坐标映射结果都是 0。
- 根因：`touch_task` 优先级(idle+4) 高于 `ui_task`(idle+2)，先跑完 `bsp_touch_init()`；
  而 `g_lcd_info.lcd_width/height` 要等 `ui_task` 里的 `lcd_driver_init()` 才填上，
  触摸侧读到的是清零后的结构体。
- 修复：`bsp_lcd.c` 在 `lcd_config_init()` 末尾置 `g_lcd_ready` 并新增 `lcd_driver_ready()`；
  `bsp_touch_init()` 先等该标志（最多 10 s），`bsp_touch_scan()` 里再刷新一次几何，
  双保险。

**"模拟 SPI"实际是 I2C（2026-08-28 澄清）**
- 排针丝印为 `T_CS/T_SCK/T_MOSI/T_MISO/T_PEN`，但 **GT9147/GT911 只有 I2C 接口**。
  板上 `T_SCK(PH6)` = `CT_SCL`、`T_MOSI(PI3)` = `CT_SDA`、`T_CS(PI8)` = `CT_RST`、
  `T_PEN(PH7)` = `CT_INT`。因此 `bsp_sw_i2c.c` 实现的是位绑定 I2C，不是 SPI。
  实测 `product ID = "911" (addr 0x14) -> MATCH` 证明总线时序正确。

**UI 布局硬编码 800×480 与实际 480×800 画布不符（2026-08-28 已修复）**
- 现象：LCD 驱动打印 `active GRAM window 480x800`，而 `app_ui.c` 用 `UI_W 800 / UI_H 480`
  排版 → 横向被裁掉 320 px、纵向空出 320 px。
- 修复：布局改为从 `lv_disp_get_hor_res/ver_res()` 取尺寸，三栏位置与高度按实际画布算。
  改画布尺寸只需动 `bsp/bsp_lcd.h` 的 `LCD_WIDTH/LCD_HEIGHT`。

**MPU9250 磁力计偶发读失败导致整包丢弃（2026-08-28 已修复）**
- 现象：`[SENS ] MPU9250 read FAILED (I2C2), retry in 30s`，第 2 页全部显示 `--` 长达 30 s。
- 根因：`bsp_mpu9250_read()` 返回 `-3` 只代表 AK8963（经 MPU 内部 I2C master 访问）读失败，
  加速度/陀螺其实读到了；原代码把 `!= 0` 一律当整体失败。
- 修复：`-3` 时仍发布加速度/陀螺，只把 `mag_ok` 置 0（页面仅"磁场"显示 `AK8963 未就绪`）；
  失败退避由 30 s 缩到 2 s；同一设备错误最多每 60 s 打印一次（避免撑爆 UART 环形缓冲）。

**错误日志首条被吞（2026-08-28 已修复）**
- 现象：限流打印用 `0xFFFFFFFFU` 作"尚未打印"哨兵，`s_round - 0xFFFFFFFF` 回绕成 2，
  永远 `< 120` → 第一条错误永远打不出来。
- 修复：哨兵改为 `0`，条件改为 `(*last_at == 0U) || ((s_round - *last_at) >= ERR_PRINT_ROUNDS)`。

**UNIGBK.BIN 缺失无害**
- `mask` 的 bit0（UNIGBK.BIN）为 0 属正常：渲染路径经编译期表 `lv_gbk_map.c`（`lv_gbk_from_unicode`）做 Unicode→GBK，再读 `GBKxx.FON` 取字模；`UNIGBK.BIN` 仅被打开、从未被读取。字库目录放齐 `GBK12/16/24/32.FON` 即可显示中文。

---

## 7. 调试 / 仿真环境（VSCode + OpenOCD + arm-none-eabi-gdb + STLink）

> 原则：工具链路径一律走环境变量，**工程文件不写死本机绝对路径**（换机器只改环境变量）。

### 7.1 配置文件清单（均在工程目录下，可移植）

| 文件 | 作用 |
|------|------|
| `openocd/stm32f429_stlink.cfg` | OpenOCD 调试/烧录配置，用 `[find interface/stlink.cfg]` + `[find target/stm32f4x.cfg]`，零硬编码路径 |
| `.vscode/launch.json` | Cortex-Debug 启动配置：`openOCDPath`/`searchDir`/`gdbPath` 全用 `${env:VAR}`；`executable`/`configFiles`/`svdFile` 用相对路径 |
| `.vscode/tasks.json` | `CMake Build (Debug/Release)` / `Clean` / `Flash with OpenOCD` 任务，工具同样走环境变量 |
| `.vscode/settings.json` | Cortex-Debug / CMake 路径解析（env 变量），`cmake.useCMakePresets=always` |
| `.vscode/c_cpp_properties.json` | IntelliSense，`compilerPath` 走 env 变量，包含目录相对路径 |
| `.vscode/extensions.json` | 推荐扩展：`marus25.cortex-debug` / `ms-vscode.cpptools` / `ms-vscode.cmake-tools` |
| `.vscode/STM32F429x.svd` | 外设寄存器描述（工程内随附，相对引用，支持 SVD 视图） |
| `debug/gdbinit` | 复用 gdb 脚本：`arm-none-eabi-gdb -x debug/gdbinit` 连常驻服务器、halt、载符号 |
| `CMakePresets.json` | `debug`/`release` 预设（相对 `binaryDir`，可移植） |

### 7.2 前置：设置环境变量（示例值，按需替换）

```bash
# Windows（系统属性 / 会话内）
set ARM_GNU_TOOLCHAIN_BIN=E:/support_tools/arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi/bin
set OPENOCD_BIN=D:/software/ST/OpenOCD/bin
set OPENOCD_SCRIPTS=D:/software/ST/OpenOCD/share/openocd/scripts
set CMAKE_BIN=C:/Software/msys2/mingw64/bin
set NINJA_BIN=C:/Software/msys2/mingw64/bin
```

### 7.3 使用方式

1. VSCode 安装推荐扩展（见 7.1）。
2. **F5 调试**：自动先 `CMake Build (Debug)` 再启动 Cortex-Debug（OpenOCD + STLink SWD + gdb），
   断点命中后可在 `main` / `OTG_FS_IRQHandler` 等下硬件断点，SVD 视图看外设寄存器。
3. **仅烧录**：运行 `Flash with OpenOCD` 任务（等价 `program ... verify reset exit`）。
4. **命令行 gdb**：先起常驻 OpenOCD，再 `arm-none-eabi-gdb -x debug/gdbinit`。

```bash
# 常驻 OpenOCD 调试服务器（gdb :3333 / telnet :4444 / tcl :6666）
openocd -s %OPENOCD_SCRIPTS% -f openocd/stm32f429_stlink.cfg
# 另一终端
arm-none-eabi-gdb -x debug/gdbinit
```

### 7.4 已验证（本次实测）

- OpenOCD 0.12.0 常驻服务器成功：`STLINK V2J46S7`、`SWD DPIDR 0x2ba01477`(F429)、
  Cortex-M4 r0p1、6 硬件断点 / 4 观察点，`Listening on port 3333`（gdb）。
- `arm-none-eabi-gdb` 连接 `:3333`、载入符号、`monitor reset halt`（PC=Reset_Handler，
  MSP=0x20030000）、在 `main()`（`main.c:74`）命中硬件断点、`info registers` / `bt` 显示
  源码级调用栈（`main → vTaskStartScheduler → OTG_FS_IRQHandler`）、`reset halt` 干净 detach。
- **固件启动确认**：gdb 调用栈证明固件已跑到 `main()` 的 `vTaskStartScheduler()`（main.c:141），
  启动横幅（I2C/SDRAM/USB Host 等）已通过 USART3(PB10) 发出 → 固件正常运行。

### 7.5 串口实测（COM5，2026-08-26 PASS）

- COM5（USART3 PB10/PB11，115200 8N1）驱动已修复、可正常打开。
- 烧录修正后固件并通过 COM5 抓取，得到**完整启动 + U 盘端到端**输出：

```
System Init
I2C / PCF8574 init OK
SDRAM Init OK
FreeRTOS Heap configured (SDRAM @0xC0000000)
Waiting for USB disk...
USB Host Init
USB Disk Connected (MSC ready)
USB Disk Mounted
========== USB DISK CONTENTS ==========
[DIR ] 0:/demo
[DIR ] 0:/demo/sub
[FILE] 0:/demo/sub/world.txt  (31 bytes)
  === content (31 bytes) ===
Nested directory file content
  === end ===
[FILE] 0:/demo/hello.txt  (31 bytes)
  === content (31 bytes) ===
Hello from STM32F429 USB Host
  === end ===
[FILE] 0:/demo/notes.txt  (24 bytes)
  === content (24 bytes) ===
Line A
Line B
Line C
  === end ===
========== END (dirs=3 files=6) ==========
Heap object @ 0xC0003F38 (SDRAM base 0xC0000000)
```

- 关键结论：
  - **`USB Host Init` 出现在 `Waiting for USB disk...` 之后**，证明 USB 硬件/栈初始化已被
    推迟到 `usbh_host_task`（OS 启动后）执行——修正了“中断触发 FromISR queue 破坏 RTOS”的
    跑飞问题。
  - **`Heap object @ 0xC0003F38`** 证实 FreeRTOS 堆确实落在外部 SDRAM（0xC0000000）。
  - **U 盘内容经串口读取并打印**：`usb_disk_explore()` 在挂载后递归遍历根目录（含子目录），
    对每个文件打印 `[DIR]/[FILE]` 行 + 文件正文（`=== content ===` 块），非打印字符被替换为
    `.`，单文件上限 `DISK_MAX_DUMP_BYTES=2048` 防止串口刷屏。
  - 可选 `USB_DISK_SEED_DEMO`（默认 1）：挂载后先向 `0:/demo/`（含子目录 `sub/`）写入若干
    确定性样本文件，使“读取打印”可被一键复测；置 0 即纯读取用户盘内既有内容。
- 一键复测：`verify_serial/explore_com5_test.py`（先开 COM5 → OpenOCD 烧录 → 复位 → 抓 18s，
  校验目录遍历/嵌套/文件正文/堆地址等 19 项特征串，输出 PASS/FAIL）。

### 7.6 启动加载流程实测（COM5，2026-08-28 PASS 17/17）

一键验收（烧录 Release + 抓 25 s + pass/fail 计数）：

```bash
cd <工程根目录>
SERIAL_PORT=COM5 python tools/verify_serial/verify_boot_flow.py
```

实测日志（microSD 未插卡、U 盘已插入字库）：

```
System Init
I2C / PCF8574 init OK
SDRAM Init OK
FreeRTOS Heap configured (SDRAM @0xC0000000)
Waiting for USB disk...
USB Host Init                                 ← USB 初始化在调度器之后
[UI  ] starting LCD + LVGL bring-up
[LCD ] controller ID = 0x8000
[LCD ] active GRAM window 480x800 (panel spec 800x480)
[UI  ] boot screen: wait for system start...   ← ASCII 启动页（无需任何文件）
[SD  ] SDIO init FAILED (no card?)             ← 先探 SD，无卡优雅降级
USB Disk Connected (MSC ready)
USB Disk Mounted
[FONT] trying USB 0:/SYSTEM/FONT/              ← 回退到 U 盘
[FONT] source=0: mask=0x1F                     ← UNIGBK+GBK12/16/24/32 全开
[UI  ] main screen (fonts from 0:)             ← 进入主界面
[UI  ] glyph cache: hits=... misses=45         ← CJK 字形确实从字库读出
```

复现 **10 s 超时分支**（两端都无介质）。U 盘已插在板上，所以用编译开关临时关掉 USB 回退：

```bash
cmake -S . -B build_to -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-DLOADER_ENABLE_USB_FALLBACK=0"
cmake --build build_to
SERIAL_PORT=COM5 FIRMWARE=build_to/stm32f429_tinyusb_ui.elf \
      python tools/verify_serial/verify_boot_flow.py
```

实测在启动页出现后第 10 s 打印一次 `[UI  ] timeout: sdcard and usb loader failed!`
（只打印一次），屏幕居中显示该字符串。

### 7.7 触摸 + 双页面实测（COM5，2026-08-28）

**7.7.1 中断链路（全自动，无需人手）**

EXTI 的 `EXTI_SWIER` 寄存器可以产生与引脚边沿完全等价的软件中断，因此
"EXTI → NVIC → ISR → 信号量 → touch_task 唤醒"整条链路可以自动验证：

```bash
SERIAL_PORT=COM5 python tools/verify_serial/verify_touch_irq.py
```

实测（PASS 7/7）：

```
[TOUCH] task started, probing GT9147 (T_SCK=PH6 T_MOSI=PI3 T_CS=PI8 T_PEN=PH7)
[TOUCH] product ID = "911" (addr 0x14) -> MATCH     ← 板上实为 GT911，寄存器兼容
[TOUCH] stored config: version=0x51 resolution=480x800
[TOUCH] EXTI cfg: EXTICR2=0x00007000 IMR=0x0080 FTSR=0x0080 prio=6
[TOUCH] ready: id=911 addr=0x14 cfg=0x51, touch 480x800 -> canvas 480x800, swap=0 invX=0 invY=0
[TOUCH] INT armed on T_PEN (PH7, falling edge) -> waiting
[TOUCH] T_PEN interrupt received (irq=2)            ← 软件注入 line 7 后任务被唤醒
[UI  ] LVGL canvas 480x800, pointer indev registered
[SENS ] AP3216C init OK
[SENS ] MPU9250 init OK (WHO_AM_I check)
```

- `EXTICR2=0x00007000`：EXTI line 7 已复用到 **GPIOH**（`0x7`）；`IMR/FTSR=0x0080`：line 7
  中断使能 + 下降沿触发。
- 芯片自报分辨率 **480×800** 与 LVGL 画布一致，恒等映射即可（无需 swap/invert）。
- 注意：本方法验证的是中断链路，**不验证 PH7 引脚电平本身**，仍需手指点一次确认。

**7.7.2 手指触摸 + 翻页（需人工，脚本已就绪）**

```bash
SERIAL_PORT=COM5 TOUCH_WAIT_SEC=30 python tools/verify_serial/verify_touch.py
```

脚本烧录后会提示：①点屏幕任意位置 ②点底部右箭头 ③点底部左箭头，并实时打印串口：

```
[TOUCH] raw=(238,412) -> lv=(238,412) points=1 irq=7   ← 引脚中断确实进来了(irq 递增)
[UI  ] page -> 2 / 2                                    ← 右箭头：进第 2 页
[UI  ] page -> 1 / 2                                    ← 左箭头：循环回第 1 页
```

判定要点：

- `irq=N` 递增 → 说明是**引脚中断**唤醒的，不是 1 s 兜底轮询。
- 若出现 `WARNING: contact seen without T_PEN interrupt` → INT 线没连到 PH7。
- 若点右箭头却翻到了上一页（或坐标左右/上下相反）→ 改 `bsp/bsp_touch.h` 里的
  `TOUCH_SWAP_XY` / `TOUCH_INVERT_X` / `TOUCH_INVERT_Y` 三个宏（各只需 0/1 切换），
  对照日志里的 `raw=` 与 `lv=` 一行即可确定。

### 7.8 PRINT_LOG 日志系统实测（2026-08-29，PASS 6/6）

本轮验证时 **COM5 (CH340) 处于驱动 code-31 故障状态**（`PermissionError(13)`，
需重新插拔 USB 转串口），因此改用 **SWD 读内存**取证：日志关掉后，
`bsp_uart.c` 的 TX 环形缓冲写指针必须一个字节都没动过。

```bash
python tools/verify_serial/verify_log_switch.py
```

```
---- PRINT_LOG 开 (build/stm32f429_tinyusb_ui.elf) ----
  g_tx_head=0x0350  g_tx_tail=0x0350  g_tx_busy=0  g_usb_state=4
  [PASS] 系统完整启动（USB 已挂载, g_usb_state=4）
  [PASS] 日志已送入 UART TX 环（g_tx_head != 0）      实测 0x0350 = 848 字节
  [PASS] TX 环已被 ISR 排空（head == tail）           无丢字节

---- PRINT_LOG 关 (build_nolog/stm32f429_tinyusb_ui.elf) ----
  g_tx_head=0x0000  g_tx_tail=0x0000  g_tx_busy=0  g_usb_state=4
  [PASS] 系统完整启动（USB 已挂载, g_usb_state=4）
  [PASS] 一个字节都没进 TX 环（g_tx_head == 0）
  [PASS] 发送器从未启动（g_tx_busy == 0）
========== RESULT: 6 passed, 0 failed / VERDICT: PASS ==========
```

要点：
- **日志关 = 串口零字节**，且系统照常启动到 USB 挂载（`g_usb_state=4`）——
  证明开关真的切断了输出，而不是把日志丢在别处。
- 日志开时 `head == tail`，说明互斥量 + 临界区入环 + ISR 排空这条路径没有丢字节。
- 符号地址由 `arm-none-eabi-nm` 从 `.elf` 取，再通过 OpenOCD `mdw` 读；
  TX 计数器是 `uint8 + uint16 + uint16` 混排、地址不对齐，所以脚本按 4 字节对齐
  整字读取后在 Python 里切字节（`mdw` 直接读非对齐地址会报 `Failed to read memory`）。

> COM5 恢复后建议再跑一遍 `verify_boot_flow.py` / `verify_touch_irq.py`，
> 用串口侧确认日志文本与改动前逐行一致。

### 7.9 I2C2 故障修复实测（2026-08-29，PASS 7/7）

**故障现象**（用户报）：系统未启动时 I2C 正常，FreeRTOS 起来后
`AP3216C / MPU9250 read FAILED (rc=-1)`，且之后每次都失败。日志同时显示
`irq=93014`、触摸坐标卡在 `raw=(436,771)` 不变。

**定位**：传感器 init 在触摸 EXTI 使能**之前**成功、之后失败 —— 指向触摸中断。
加了 IRQ 速率看门狗后一击命中：

```
[TOUCH] WARNING: T_PEN interrupt storm 46925/s (> 1000/s) - check PH7 pull-up / crosstalk from PH6
```

**修复后**（`tools/verify_serial/verify_sensors.py`，SWD 直读 `s_data`）：

```
AP3216C : ok=1  IR=5   环境光=4 lux  接近=30
MPU9250 : ok=1 (mag=0)
  加速度  ax=-0.03 ay=+0.01 az=+1.00 g      |a| = 1.00 g  ← 板子平放，重力全在 Z
  角速度  gx=-0.1 gy=+0.2 gz=-2.1 dps
  统计    : samples=38  errors=0   AK8963 WIA=0x00
RESULT: 7 passed, 0 failed -> PASS
```

- **`errors=0`**：修复前每 2 s 就报一次 FAILED，现在整轮 22 s 零错误。
- **`samples=38`** 正常推进，环境光随光照在 0 → 4 → 10 lux 变化，传感器确实活着。
- **AK8963 WIA=0x00**：本模块**没有可用磁力计**（硬件事实）。改前代码把全零当
  "读取成功"上报，改后 init 返回 `-3`、页面显示「磁场  AK8963 未装配」。

> COM5 已恢复，启动流程回归 `verify_boot_flow.py` 17/17、`verify_touch_irq.py` 7/7 均通过。
> 仍未完成的只有**手指触摸 + 翻页**的人工确认（`verify_touch.py`）。

---

## 8. 目录结构

```
102.stm32f429_tinyusb_ui/
├── app/
│   ├── main.c                     # 初始化顺序 + 系统时钟 + 任务创建
│   ├── usb_host_app.c/.h          # TinyUSB MSC 主机任务 + U 盘内容递归读取/打印（带 dump 限流）
│   ├── fs_diskio.c/.h             # ★ 统一 FatFs diskio 胶水：pdrv0=USB MSC, pdrv1=SDIO + fs_lock
│   ├── sd_card.c/.h               # ★ microSD 上电流程：SDIO 初始化 + f_mount("1:")
│   ├── ui_task.c/.h               # ★ 启动加载状态机 + LVGL 渲染泵 + LVGL indev 注册
│   ├── app_ui.c/.h                # ★ UI 框架：布局 / 导航条 / 2 Hz tick / 页面分发 / 公共 API
│   ├── ui/                        # ★ 每页独立文件，共享声明在 ui_common.h
│   │   ├── ui_common.h            #   几何宏 / 页枚举 / 各页 widget 句柄 / extern 全局 / 辅助&构建原型
│   │   ├── page_status.c          #   页 0：USB / 字库 / SD / 运行时长
│   │   ├── page_hwinfo.c          #   页 1：AP3216C 光感 + MPU9250 传感器
│   │   ├── page_ctrl.c            #   页 2：LED / 蜂鸣器控制
│   │   └── page_rtc.c             #   页 3：RTC 时钟 + 时间/闹钟设置 + 报警 60s 自动关
│   ├── log.c/.h                   # ★ PRINT_LOG 日志系统（全局 PRINT_LOG_ENABLE 开关）
│   ├── touch_task.c/.h            # ★ T_PEN 中断 → 信号量 → GT9147 轮询 → 发布坐标
│   ├── sensor_task.c/.h           # ★ AP3216C + MPU9250 周期采样（500 ms）
│   ├── tusb_config.h              # TinyUSB 主机配置 (CFG_TUH_MSC=1)
│   ├── stm32f4xx_hal_timebase_tim.c  # TIM7 1 ms 时基
│   ├── stm32f4xx_it.c             # 中断向量 (TIM7/OTG_FS/EXTI9_5/FreeRTOS 端口)
│   ├── sdram_heap.c               # heap_5 单区 (SDRAM 0xC0000000)
│   ├── FreeRTOSConfig.h / syscalls.c / stm32f4xx_hal_conf.h
├── bsp/
│   ├── bsp_uart.c   (USART3 PB10/11)
│   ├── bsp_led.c/.h (PB0/PB1)
│   ├── bsp_i2c.c    (I2C2 PH4/5 + 总线恢复)
│   ├── bsp_pcf8574.c (BEEP P0 / ETH 复位 P7)
│   ├── bsp_sdram.c  (FMC W9825G6KH-6)
│   ├── bsp_sdio.c/.h (★ microSD SDIO 4bit，PC8-12 + PD2，轮询无 DMA)
│   ├── bsp_sw_i2c.c/.h  (★ 软件位绑定 I2C：PH6=SCL / PI3=SDA，给触控芯片用)
│   ├── bsp_gt9147.c/.h  (★ GT9147/GT911 驱动：复位寻址 + ID 校验 + 配置块 + 5 点坐标)
│   ├── bsp_touch.c/.h   (★ T_PEN EXTI + 二值信号量 + 坐标映射到 LVGL 画布)
│   ├── lv_port_indev.c/.h (★ LVGL pointer indev，只读 bsp_touch 发布的状态)
│   ├── bsp_usb_hw.c (OTG_FS 时钟/引脚/中断)
│   └── bsp_ap3216 / bsp_mpu9250 / bsp_eeprom_24c02 / bsp_delay
├── Drivers/                        # CMSIS + STM32F4 HAL
├── third_party/
│   ├── FreeRTOS-Kernel/           # V11，heap_5
│   ├── FatFs/                     # R0.15，FF_FS_EXFAT=1
│   └── tinyusb/                   # 0.21.0 主机栈
├── ldscript/STM32F429IGTx_FLASH.ld
├── openocd/stm32f429_stlink.cfg  # OpenOCD 调试/烧录配置（env 变量 + [find]）
├── .vscode/                       # VSCode 调试/IntelliSense（launch/tasks/settings/c_cpp/extensions + SVD）
├── debug/gdbinit                  # 复用 gdb 脚本（连常驻 openocd、halt、载符号）
├── CMakePresets.json              # 可移植构建预设（debug/release）
├── verify_exfat/                  # PC 端 exFAT 真实性验证工具
├── tools/verify_serial/
│   ├── verify_boot_flow.py        # ★ 启动加载流程验收（烧录 + COM5 抓 25s + pass/fail）
│   ├── verify_touch_irq.py        # ★ T_PEN 中断链路验收（SWIER 软注入，全自动 7/7）
│   ├── verify_touch.py            # ★ 手指触摸 + 翻页验收（烧录 + 交互式，需人工点屏）
│   ├── verify_log_switch.py       # ★ PRINT_LOG 开关验收（串口不可用时改走 SWD 读内存）
│   ├── capture_reset.py           # ★ 只复位不烧录，抓控制类日志（--raw 看全量）
│   └── verify_ui_com5.py / explore_com5_test.py / ...   # 既有 U 盘遍历验收
├── CMakeLists.txt / cmake/arm-none-eabi-gcc.cmake
├── README.md
└── ACCEPTANCE_REPORT.md
```
