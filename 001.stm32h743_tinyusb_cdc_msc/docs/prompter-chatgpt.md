# STM32H743ZIT6 TinyUSB CDC+MSC USB复合设备开发任务

## 1. 项目目标

从零构建一个基于 STM32H743ZIT6 + TinyUSB 的 VSCode / CMake / GCC / MDK-ARM 双开发环境工程，实现 USB CDC + MSC 复合设备。

最终目标：

1. USB 连接 PC 后，同时枚举：

   * USB CDC 虚拟串口
   * USB MSC 大容量存储设备
2. CDC 可以正常双向通信。
3. MSC 可以正常被 Windows/Linux 主机识别并访问。
4. MSC 后端使用 STM32 外部 SD 卡。
5. CDC 与 MSC 可以同时工作，互不阻塞、互不干扰。
6. 项目支持：

   * VSCode + CMake + Ninja + GCC + Cortex-Debug + OpenOCD + ST-Link
   * Keil MDK-ARM + `stm32h743.uvprojx`
7. 两套开发环境共用同一套源码，不复制业务代码。
8. 异常优先通过实际运行、日志、断点、单步调试定位，而不是猜测修改。
9. 项目完成后更新 README.md，记录工程结构、编译方法、调试方法、USB 架构、实际遇到的问题及解决方案。

---

# 2. 开发原则

不要一次性生成全部代码。

严格按照以下阶段执行：

* Phase 0：工程基础设施
* Phase 1：STM32基础启动 + LED + USART1
* Phase 2：SDMMC + FatFs 单独验证
* Phase 3：TinyUSB CDC
* Phase 4：TinyUSB MSC
* Phase 5：CDC + MSC Composite
* Phase 6：并发压力测试
* Phase 7：调试与稳定性优化
* Phase 8：README总结

每完成一个阶段：

1. 编译
2. 下载
3. 运行
4. 验证
5. 出现问题先定位
6. 修复
7. 再进入下一阶段

禁止前一阶段未通过就盲目进入下一阶段。

核心开发流程：

```text
生成 → 构建 → 烧录 → 运行 → 测试 → 定位 → 修复 → 验收
```

---

# 3. 硬件资源

## MCU

STM32H743ZIT6

## 时钟

HSE：

25 MHz 外部无源晶振

LSE：

32.768 kHz 外部无源晶振

根据 STM32H743 时钟树正确配置系统时钟。

不要机械套用其他 STM32H7 工程的 PLL 参数。

启动后确认：

* SYSCLK
* HCLK
* APB1
* APB2
* USB 48 MHz 时钟

均满足 STM32H743 HAL 和 USB 外设要求。

---

# 4. USB硬件

本项目明确使用：

**STM32H743 USB OTG FS**

不是 USB HS。

USB连接：

```text
PA11 = USB_DM
PA12 = USB_DP
```

要求：

1. 使用 USB OTG FS。
2. USB 工作在 Full-Speed 模式。
3. 不使用外部 ULPI PHY。
4. 不配置 USB HS ULPI。
5. 正确配置 USB FS 48 MHz 时钟。
6. TinyUSB 使用 STM32 USB FS Device。
7. README 中明确记录 USB FS 配置。

---

# 5. GPIO / 外设配置

## LED

PG7

* 输出
* 推挽
* 上拉
* 低电平点亮

提供：

```c
led_init();
led_on();
led_off();
led_toggle();
```

用于启动及 USB 调试状态指示。

---

# 6. USART1

```text
PA9  = USART1_TX
PA10 = USART1_RX
```

主要用于调试日志。

建议：

```text
115200 8N1
```

提供简单 printf/log 输出。

日志不能阻塞 USB 数据处理，后续应可以关闭或降低日志等级。

---

# 7. QSPI W25Q64

QSPI Flash：

```text
CS   = PG6  AF10
CLK  = PF10 AF9
IO0  = PF8  AF10
IO1  = PF9  AF10
IO2  = PF7  AF9
IO3  = PF6  AF9
```

当前主要用于板级资源预留和后续扩展。

初始化代码应与 SD、USB 等模块保持独立，不允许与 Storage Layer 强耦合。

---

# 8. SDMMC

SD 卡作为 MSC 存储介质。

使用 SDMMC 4-bit：

```text
CK  = PC12 AF12
CMD = PD2  AF12
D0  = PC8  AF12
D1  = PC9  AF12
D2  = PC10 AF12
D3  = PC11 AF12
```

要求：

1. SD 卡初始化。
2. 获取容量。
3. 读取 block。
4. 写入 block。
5. 数据校验。
6. 接入 FatFs。
7. 完成独立 SD/FatFs 测试。
8. SD 卡稳定读写后才能进入 MSC。

---

# 9. SPI6 OLED ST7789

```text
SCK  = PG13 AF5
MOSI = PG14 AF5
CS   = PG8  GPIO
DC   = PG15 GPIO
BL   = PG12 GPIO
```

OLED 驱动与 USB、SD 等模块保持独立。

当前工程重点仍然是 USB CDC + MSC，不因为 OLED 增加复杂 UI 框架。

---

# 10. DCMI / OV5640

I2C：

```text
SCL = PF14
SDA = PF15
```

控制：

```text
PWDN = PF13
```

低电平有效。

DCMI：

```text
HSYNC = PA4
VSYNC = PG9
D0    = PC6
D1    = PC7
D2    = PG10
D3    = PG11
D4    = PE4
D5    = PD3
D6    = PE5
D7    = PE6
CLK   = PA6
```

摄像头功能属于后续扩展资源。

当前阶段不要因为 DCMI/OV5640 引入额外复杂依赖。

---

# 11. 工程目录与代码风格

项目主要目录：

```text
project/
├── app/                  # 应用逻辑
│   ├── main.c
│   └── usb/
├── build/                # cmake输出目录
├── bsp/                  # 用户硬件驱动
│   ├── bsp.c
│   ├── bsp_led.c
│   ├── bsp_log.c
│   ├── bsp_log.h
│   └── bsp_xxx.x
├── Drivers/              # STM32 HAL/CMSIS
├── third_party/          # 第三方库
│   ├── tinyusb/
│   └── fatfs/
├── sys_startup/          # 启动文件、链接文件
│   └── stm32h743xx_flash.ld
├── MDK-ARM
│   └── mdk_target.c
│   └── stm32h743.sct
│   └── stm32h743.uvprojx
├── tools/                # PC测试脚本
├── cmake/
├── doc/                  # 提示词和文档，不要手动修改
├── .vscode/
├── openocd.cfg
├── build_oneclick.bat    # 使用cmake进行编译
├── CMakeLists.txt
├── STM32H743.svd
└── README.md
```

`sys_startup/` 专门用于：

* STM32H743 启动文件
* `.ld` Linker Script
* 与 MCU 启动和内存布局相关的文件

不再使用 `linker/` 目录。

目录职责：

```text
app          = 应用逻辑
bsp          = 用户硬件驱动
Drivers      = STM32 HAL/CMSIS
third_party  = 第三方库
sys_startup  = 启动文件和链接配置
tools        = PC测试工具
```

优先复用现有工程结构，不进行无意义重构。

---

# 12. 代码风格

统一采用：

* C语言
* 4空格缩进
* Allman大括号
* 小写 snake_case 文件名、函数名、变量名
* 私有函数和变量优先使用 `static`
* 注释使用简洁英文
* 不使用 Tab
* 保持现有代码风格
* 不进行无意义格式化或重构

代码参考：

```c
/* Enable the USART1 global interrupt once (idempotent). */

static void uart_tx_enable_irq(void)
{
    if (!uart_tx_nvic_on)
    {
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        uart_tx_nvic_on = 1U;
    }
}
```

函数命名：

```c
bsp_usb_init();
bsp_usb_task();

bsp_uart_init();
bsp_uart_write();

bsp_led_init();
bsp_led_on();
bsp_led_off();
bsp_led_toggle();
```

文件命名：

```text
usb_descriptors.c
usb_descriptors.h
bsp_sdcard.c
bsp_sdcard.h
bsp_uart.c
bsp_uart.h
```

新增代码、文件和模块自动遵循以上规则。

---

# 13. CMake + Ninja

CMake 是 GCC 开发环境的主要构建入口。

使用：

```text
CMake
Ninja
arm-none-eabi-gcc
```

提供：

```text
Debug
Release
```

Debug：

```text
-Og
-g3
```

必须支持：

```bash
cmake -S . -B build/debug -G Ninja
ninja -C build/debug
```

直接完成编译。

不要依赖 IDE 自动生成代码。

---

# 14. MDK-ARM

工程必须同时支持 Keil MDK-ARM。

MDK 工程文件固定为：

```text
stm32h743.uvprojx
```

可以直接使用：

```text
stm32h743.uvprojx
```

完成：

* 编译
* 下载
* Debug
* 单步
* 断点
* 寄存器查看
* Memory 查看
* Flash 下载

MDK 工程与 CMake 工程必须使用同一套源码：

```text
app/
bsp/
Drivers/
third_party/
sys_startup/
```

禁止复制一套源码到：

```text
MDK/
Keil/
Project/
```

等目录。

---

# 15. MDK工程配置

`stm32h743.uvprojx` 必须直接引用项目中的实际源码。

正确配置：

* Device：STM32H743ZIT6
* ARM Compiler
* C/C++ Include Paths
* Preprocessor Definitions
* Source Groups
* Startup File
* Flash Download
* Debug Driver

MDK 工程必须能够独立完成编译、下载和调试。

---

# 16. 启动文件与Linker

启动文件：

```text
sys_startup/startup_stm32h743xx.s
```

GCC Linker Script：

```text
sys_startup/stm32h743xx_flash.ld
```

CMake 使用 `.ld` 文件。

MDK 使用对应的 Scatter File / Target Linker 配置。

CMake 和 MDK 的：

* Flash 地址
* Flash 大小
* RAM 地址
* RAM 大小
* Stack
* Heap
* 启动文件
* MCU型号

必须保持一致。

如果 MDK 使用 `.sct`，可以放在：

```text
sys_startup/
```

例如：

```text
sys_startup/stm32h743xx.sct
```

不要在工程根目录增加独立 linker 目录。

---

# 17. CMake与MDK一致性

CMake 和 MDK 必须保持以下内容一致：

```text
MCU
CPU架构
FPU
浮点ABI
宏定义
Include Path
源码文件
HAL配置
TinyUSB配置
FatFs配置
RAM布局
Flash布局
```

例如：

```text
STM32H743xx
USE_HAL_DRIVER
```

等必要宏定义必须保持一致。

如果 CMake 和 MDK 使用不同配置，必须明确记录原因。

---

# 18. VSCode / Cortex-Debug

提供：

```text
.vscode/launch.json
.vscode/tasks.json
```

支持：

* OpenOCD
* ST-Link
* ELF 调试
* 下载
* Reset
* Halt
* Continue
* Step Over
* Step Into
* 变量查看
* 寄存器查看
* 调用栈查看

调试配置不得使用绝对路径。

---

# 19. TinyUSB

TinyUSB 放置：

```text
third_party/tinyusb/
```

要求：

1. 使用官方 TinyUSB 源码。
2. 不修改 TinyUSB 核心源码，除非确实必要。
3. 如果修改，必须记录原因。
4. MCU 相关适配代码与 TinyUSB 核心代码分离。
5. USB Descriptor 放在项目自己的模块中。
6. 使用 TinyUSB Composite Device 实现 CDC + MSC。

---

# 20. Phase 0：工程基础设施

第一次执行必须：

1. 检查当前目录。
2. 检查已有工程。
3. 检查工具链。
4. 检查 CMake。
5. 检查 Ninja。
6. 检查 arm-none-eabi-gcc。
7. 检查 MDK-ARM工程。
8. 检查 VSCode/Cortex-Debug。
9. 检查 OpenOCD。
10. 检查 ST-Link。
11. 检查 Python。
12. 检查已有 Drivers。
13. 检查已有 TinyUSB。
14. 检查已有 FatFs。
15. 确认 USB FS 硬件连接。
16. 再开始创建或修改工程。

如果已有：

```text
Drivers
third_party
bsp
app
sys_startup
```

优先复用。

不要无意义重构。

第一次只完成：

* 环境检查
* 硬件确认
* 工程结构
* CMake
* MDK工程
* STM32H743启动
* LED
* USART1
* CMake编译
* MDK编译
* 下载
* 运行

完成后暂停并报告：

```text
环境检查结果
硬件确认结果
工程结构
CMake编译结果
MDK编译结果
当前运行结果
发现的问题
下一阶段计划
```

---

# 21. Phase 1：基础工程

首先只实现：

* STM32启动
* 时钟
* LED
* USART1

完成：

1. CMake编译。
2. MDK编译。
3. 下载。
4. LED闪烁。
5. USART1输出启动信息。

例如：

```text
System Init OK
STM32H743ZIT6
Clock Init OK
```

确认基础工程正常后继续。

---

# 22. Phase 2：SD卡

实现：

```text
SDMMC 4-bit
FatFs
```

测试：

1. mount
2. 创建文件
3. 写入文件
4. 读取文件
5. 比较内容
6. 删除文件
7. 获取容量

输出：

```text
SD Init OK
SD Capacity: xxx MB
FatFs Mount OK
File Write OK
File Read OK
```

失败时优先检查：

1. 时钟
2. GPIO AF
3. SDMMC 初始化
4. SD 卡供电
5. DMA
6. Cache
7. MPU
8. FatFs 配置

不要同时修改大量配置。

---

# 23. STM32H7 Cache / DMA

STM32H743 存在 I-Cache / D-Cache。

SDMMC DMA、USB DMA 等涉及 DMA Buffer 时必须正确处理 Cache 一致性。

必须明确：

* DMA Buffer 位于哪个 RAM 区域。
* DMA Buffer 是否 Cacheable。
* 何时 Clean Cache。
* 何时 Invalidate Cache。

需要时使用：

```c
SCB_CleanDCache_by_Addr();
SCB_InvalidateDCache_by_Addr();
```

如果使用 MPU Non-Cacheable 区域，必须明确区域地址、大小及原因。

USB CDC/MSC Buffer 和 SDMMC DMA Buffer 必须避免 Cache 一致性问题。

DMA Buffer 不允许随意放入 DTCM。

CMake 和 MDK 必须使用一致的内存布局。

---

# 24. Phase 3：TinyUSB CDC

首先只实现 CDC。

设备：

```text
USB CDC ACM
```

PC 应能够枚举：

Windows：

```text
虚拟 COM
```

Linux：

```text
/dev/ttyACM*
```

VID/PID 可以使用开发测试值，但必须在 README 中说明。

---

# 25. CDC功能

实现：

```text
USB → MCU
MCU → USB
```

基础功能：

PC发送：

```text
hello
```

设备返回：

```text
echo: hello
```

同时支持持续发送：

```text
CDC alive
```

例如每1秒发送一次。

---

# 26. CDC测试

测试：

1. 短数据
2. 64 Byte
3. 256 Byte
4. 1 KB
5. 10 KB
6. 连续大量数据
7. PC连续发送
8. MCU连续发送
9. 双向同时发送

要求：

* 不死锁
* 不丢数据
* 不阻塞 USB Task

---

# 27. TinyUSB CDC编程原则

使用 TinyUSB 推荐 API：

```c
tud_cdc_connected();
tud_cdc_available();
tud_cdc_read();
tud_cdc_write();
tud_cdc_write_flush();
```

USB Device Task 必须持续运行：

```c
tud_task();
```

避免：

```c
HAL_Delay();
```

以及：

* 长时间 SD 卡操作
* 阻塞式 printf
* 阻塞式等待
* 阻塞 USB Task

不要让 CDC 操作阻塞主循环。

---

# 28. Phase 4：TinyUSB MSC

CDC 单独稳定后，再加入 MSC。

数据路径：

```text
PC
 ↓
USB MSC
 ↓
TinyUSB
 ↓
Block Device
 ↓
SDMMC
 ↓
SD Card
```

实现 TinyUSB MSC 所需：

```text
Inquiry
Test Unit Ready
Capacity
Read10
Write10
```

等操作。

---

# 29. MSC Block接口

设计独立 Storage Block Layer：

```c
storage_read();
storage_write();
storage_get_block_count();
storage_get_block_size();
```

不要在 USB MSC 回调中执行复杂、长时间阻塞操作。

---

# 30. MSC与FatFs

PC 作为 USB Host 访问 MSC 时：

PC 自己负责 FAT/exFAT 文件系统。

MCU 的 FatFs 主要用于本地 SD 卡测试。

不能让 TinyUSB MSC 和 FatFs 同时修改同一个 SD 卡文件系统。

默认：

MSC 工作期间，MCU 不再通过 FatFs 修改同一文件系统。

明确 Storage Ownership：

```text
LOCAL_FS
USB_MSC
```

USB MSC 工作时禁止本地 FatFs 修改 SD 卡。

---

# 31. Phase 5：CDC + MSC Composite

实现：

```text
CDC + MSC Composite Device
```

USB Descriptor 包含：

* Device Descriptor
* Configuration Descriptor
* CDC Communication Interface
* CDC Data Interface
* MSC Interface
* Endpoint Descriptor
* String Descriptor

要求：

* Interface Number 不冲突
* Endpoint Address 不冲突
* FIFO / Buffer 配置正确
* USB FS Endpoint 最大包长配置正确

最终：

```text
USB Device
├── CDC Communication Interface
├── CDC Data Interface
└── MSC Interface
```

PC 应同时出现：

```text
COM Port
Removable Disk
```

---

# 32. Phase 6：CDC + MSC并发测试

测试：

### 测试A

PC复制文件到U盘。

同时：

PC打开CDC串口持续发送数据。

### 测试B

CDC持续发送数据。

同时：

PC持续读取U盘文件。

### 测试C

CDC双向大量数据。

同时：

MSC持续读写。

### 测试D

大文件复制。

同时：

CDC持续运行。

重点观察：

1. USB是否掉线。
2. CDC是否丢数据。
3. MSC是否出现 I/O Error。
4. SD卡是否异常。
5. MCU是否 HardFault。
6. 是否出现 Endpoint Stalled。
7. 是否死锁。
8. 是否 watchdog reset。
9. 是否内存破坏。
10. 是否 Cache 一致性问题。

---

# 33. HardFault调试

如果发生：

```text
HardFault
BusFault
MemManage
UsageFault
```

实现基础 Fault Handler，并保存：

```text
R0
R1
R2
R3
R12
LR
PC
xPSR
CFSR
HFSR
BFAR
MMFAR
```

必须能够通过调试器分析。

禁止简单进入：

```c
while (1)
{
}
```

后不分析原因。

---

# 34. USB Buffer

合理规划：

```text
CDC RX
CDC TX
MSC Buffer
USB Endpoint Buffer
```

要求：

* 正确对齐
* Cache安全
* 不越界
* 不与普通应用 Buffer 冲突
* 位于 DMA 可访问 RAM

Debug 阶段可以适当增加 Buffer，但不能用增加 Buffer 掩盖内存越界或逻辑错误。

---

# 35. USB Descriptor

实现：

```text
Device Descriptor
Configuration Descriptor
String Descriptor
```

支持：

```text
Manufacturer
Product
Serial Number
```

推荐：

```text
Manufacturer:
STM32-TinyUSB

Product:
STM32H743 CDC+MSC
```

Serial 使用 MCU Unique ID 生成，不能每次随机变化。

开发阶段允许使用测试 VID/PID，并在 README 中说明。

---

# 36. 调试策略

所有问题按照：

```text
现象
 ↓
日志
 ↓
最小复现
 ↓
确认模块
 ↓
断点
 ↓
寄存器
 ↓
定位原因
 ↓
修复
 ↓
重新测试
```

执行。

不要因为“可能是 USB 问题”而同时修改大量配置。

---

# 37. USB问题排查

USB无法枚举时依次检查：

1. GPIO
2. USB FS模式
3. USB 48 MHz时钟
4. USB Device Controller
5. TinyUSB `tud_task()`
6. Device Descriptor
7. Configuration Descriptor
8. Endpoint
9. CDC
10. MSC

---

# 38. MSC问题排查

如果 U 盘能够枚举但打不开：

检查：

```text
Inquiry
Capacity
Block Size
Block Count
Read10
Write10
```

重点记录：

```text
LBA
Block Count
Block Size
Read/Write Length
返回状态
```

---

# 39. SD卡与MSC调试

不要在 MSC 回调中大量打印 UART。

采用计数器和错误状态记录：

```text
msc_read_count
msc_write_count
msc_error_count
sd_error_count
last_lba
last_error
```

避免：

```text
USB → SDMMC → UART
```

形成严重时序影响。

---

# 40. 自动测试

提供：

```text
tools/test_cdc.py
```

支持：

1. 自动打开 COM。
2. 发送数据。
3. 接收数据。
4. Echo 校验。
5. 大数据量测试。
6. 吞吐量统计。
7. 错误统计。

例如：

```bash
python tools/test_cdc.py COMx
```

不要依赖固定 COM 编号。

---

# 41. MSC测试

PC侧测试：

1. 检测移动磁盘。
2. 创建测试文件。
3. 写入随机数据。
4. 读取文件。
5. SHA256校验。

测试文件：

```text
usb_test.bin
```

逐步测试：

```text
1 MB
10 MB
50 MB
```

根据 SD 卡实际容量调整。

---

# 42. 长时间压力测试

最终至少进行：

```text
30分钟 CDC + MSC 并发测试
```

提供：

```text
tools/stress_test.py
```

记录：

```text
时间
CDC发送量
CDC接收量
MSC读写量
错误数量
USB重连次数
```

---

# 43. LED状态

建议：

```text
USB未连接：1Hz
USB已连接：2Hz
异常：快速闪烁
```

具体实现可以根据实际需求调整。

---

# 44. 禁止事项

禁止：

1. 使用 CubeMX 生成一套代码后再随意覆盖 TinyUSB。
2. 大量修改 TinyUSB 核心源码。
3. 在 USB 回调中使用长时间阻塞函数。
4. 在 USB 回调中使用 HAL_Delay。
5. 在 MSC 回调中执行复杂 FatFs 文件操作。
6. CDC 和 MSC 共用未经保护的全局 Buffer。
7. 忽略 STM32H7 Cache。
8. DMA Buffer 放入 DTCM。
9. 使用绝对路径。
10. 编译不通过就进入下一阶段。
11. 使用 delay 掩盖时序问题。
12. 使用增加 Buffer 掩盖内存越界。
13. HardFault 后直接重新下载而不分析。
14. 配置 USB HS 或 ULPI。
15. 未确认 SDMMC DMA/Cache 就直接实现 MSC。
16. 无意义修改已有工程结构。
17. 无意义重构已经正常工作的代码。
18. 为 MDK 和 CMake 复制两套业务源码。
19. 使用独立 `linker/` 目录，启动和链接文件统一放入 `sys_startup/`。

---

# 45. 验收标准

## CMake编译

```bash
cmake -S . -B build/debug -G Ninja
ninja -C build/debug
```

必须成功。

## MDK编译

使用：

```text
stm32h743.uvprojx
```

必须能够成功编译。

## 下载

通过：

```text
VSCode
Cortex-Debug
OpenOCD
ST-Link
```

能够下载并启动。

同时：

```text
Keil MDK-ARM
stm32h743.uvprojx
```

也能够下载并启动。

## CDC

PC能够识别 COM。

发送：

```text
hello
```

返回：

```text
echo: hello
```

连续运行至少30分钟。

## MSC

PC能够识别移动磁盘。

能够：

```text
创建文件
读取文件
写入文件
删除文件
复制文件
```

## Composite

USB连接后同时出现：

```text
CDC COM
MSC Disk
```

## 并发

CDC持续通信的同时 PC 读写 U 盘。

要求：

* CDC不掉线
* MSC不掉盘
* 数据不损坏
* 系统不HardFault
* 无明显死锁

---

# 46. README

完成项目后更新 README.md。

包含：

```text
项目介绍
硬件平台
USB FS配置
USB CDC + MSC架构
工程目录
CMake编译环境
MDK-ARM开发环境
stm32h743.uvprojx说明
编译方法
下载方法
调试方法
SD卡配置
TinyUSB配置
CDC使用方法
MSC使用方法
Cache/DMA注意事项
USB Buffer设计
测试方法
压力测试结果
已知问题
故障排查
开发过程
```

只记录实际发生的问题和实际测试结果，不要编造测试结果。

---

# 47. Agent执行要求

你现在是负责实际完成该工程的嵌入式开发 Agent。

第一次执行必须：

1. 检查当前目录。
2. 检查已有工程。
3. 检查工具链。
4. 检查 CMake。
5. 检查 Ninja。
6. 检查 arm-none-eabi-gcc。
7. 检查 MDK-ARM。
8. 检查 `stm32h743.uvprojx`。
9. 检查 VSCode/Cortex-Debug。
10. 检查 OpenOCD。
11. 检查 ST-Link。
12. 检查 Python。
13. 检查已有 Drivers。
14. 检查已有 TinyUSB。
15. 检查已有 FatFs。
16. 检查 `sys_startup/`。
17. 确认 USB FS 硬件连接。
18. 再开始创建或修改工程。

如果已有：

```text
Drivers
third_party
bsp
app
sys_startup
```

优先复用。

不要无意义重构。

---

# 48. 最终目标

不要追求“一次生成成功”。

核心目标不是生成大量源码，而是最终交付：

**一个能够实际运行的 STM32H743ZIT6 USB FS TinyUSB CDC + MSC 复合设备工程。**

同时支持两套开发入口：

```text
VSCode
 ↓
CMake
 ↓
Ninja
 ↓
arm-none-eabi-gcc
 ↓
OpenOCD + ST-Link
```

以及：

```text
Keil MDK-ARM
 ↓
stm32h743.uvprojx
 ↓
ARM Compiler
 ↓
ST-Link
```

两套工程共用同一套：

```text
app/
bsp/
Drivers/
third_party/
sys_startup/
```

源码。

必须通过：

```text
构建
↓
下载
↓
运行
↓
USB枚举
↓
CDC测试
↓
MSC测试
↓
CDC + MSC并发测试
↓
稳定性验证
```

最终保证：

**CDC 和 MSC 可以同时稳定工作，互不干扰。**
