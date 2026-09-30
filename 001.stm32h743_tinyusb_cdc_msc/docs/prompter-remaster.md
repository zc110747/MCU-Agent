# STM32F429IGT6 TinyUSB CDC+MSC 全新项目开发任务

## 1. 你的角色

你是一名资深 STM32 USB / TinyUSB / RTOS / CMake / GCC / OpenOCD 嵌入式工程师。

当前任务不是修改原有 STM32H743 工程，而是：

> 以 `MCU-Agent/001.stm32h743_tinyusb_cdc_msc` 为功能和架构参考，重新创建一个完全独立的 STM32F429IGT6 工程。

参考工程：

`https://github.com/zc110747/MCU-Agent/tree/main/001.stm32h743_tinyusb_cdc_msc`

参考工程当前实现的是：

* STM32H743ZIT6
* TinyUSB
* USB CDC + MSC Composite Device
* SDIO + FatFs
* SD 卡通过 USB MSC 暴露给 PC
* 固件侧同时可以访问 FatFs
* CMake + Ninja + arm-none-eabi-gcc
* OpenOCD + ST-Link + SWD
* VSCode Cortex-Debug
* MDK-ARM
* 独立 `sys_startup` 启动文件体系
* PC Python 自动化测试

参考工程的 README 明确要求先验证 CDC，再增加 MSC，并最终验证 CDC 与 MSC 并行工作。

---

# 2. 最重要的移植原则

## 2.1 必须创建全新工程

不要修改：

`001.stm32h743_tinyusb_cdc_msc`

不要通过修改 H743 工程 MCU 宏的方式完成移植。

必须创建新的独立工程，例如：

`002.stm32f429_tinyusb_cdc_msc`

或者：

`stm32f429_tinyusb_cdc_msc`

最终工程必须可以脱离 H743 工程独立：

* 编译
* 烧录
* 调试
* 运行
* 测试

---

# 3. STM32 MCU

目标 MCU：

`STM32F429IGT6`

必须按照 STM32F429IGT6 的真实芯片资源进行设计。

不要假设 H743 外设、寄存器、时钟树、DMA、Cache、USB 控制器行为可以直接复用。

重点重新确认：

* Cortex-M4
* Flash 容量
* SRAM 分区
* USB OTG FS/HS 能力
* SDIO
* DMA
* GPIO Alternate Function
* RCC
* PLL
* NVIC
* USB OTG 中断
* SDIO DMA
* SRAM 地址布局
* linker script

所有与 H743 强相关的内容必须重新实现。

---

# 4. 硬件接口

目标硬件：

## MCU

`STM32F429IGT6`

## USB

优先使用：

`USB OTG FS`

默认：

* PA11 = USB_DM
* PA12 = USB_DP

USB 用途：

USB CDC + MSC Composite Device。

PC 枚举后应同时出现：

1. USB Virtual COM Port
2. USB Mass Storage Device

---

# 5. SD 卡接口

使用 STM32F429 的：

`SDIO`

连接板载 / 外部 microSD 卡。

必须根据 STM32F429IGT6 的实际硬件连接重新确认：

* SDIO_CK
* SDIO_CMD
* SDIO_D0
* SDIO_D1
* SDIO_D2
* SDIO_D3

如果实际开发板的 SDIO 引脚与常见默认映射不同：

> 以实际硬件连接为准，不允许为了套用参考工程而修改硬件定义。

SD 卡功能：

```text
STM32F429
    |
    +--- SDIO
           |
           +--- FatFs
           |
           +--- TinyUSB MSC
                   |
                   USB
                   |
                   PC
```

---

# 6. USB 功能

必须实现：

## USB Composite Device

```text
USB Device
├── CDC
│   ├── Virtual COM Port
│   └── Bidirectional communication
│
└── MSC
    └── SD Card
```

CDC：

* PC 可以打开 COM 端口
* MCU 可以接收数据
* MCU 可以发送数据
* 支持长数据连续传输
* 支持 CDC Echo / Loopback 测试

MSC：

* PC 可以识别 USB Mass Storage
* PC 可以读取 SD 卡
* PC 可以写入 SD 卡
* 文件系统使用 FatFs
* USB MSC 底层访问 SD 卡 Block Device

---

# 7. MSC 与 FatFs 并发访问

这是本项目的重点风险之一。

必须明确：

PC 通过 USB MSC 访问 SD 卡时：

```text
PC
 ↓
USB MSC
 ↓
TinyUSB MSC
 ↓
Disk Interface
 ↓
SDIO
 ↓
SD Card
```

MCU 内部 FatFs：

```text
Application
 ↓
FatFs
 ↓
diskio
 ↓
SDIO
 ↓
SD Card
```

两条路径最终访问同一张 SD 卡。

必须设计明确的访问策略。

禁止简单认为：

```c
f_open()
```

和：

```c
tud_msc_read10_cb()
```

可以无锁并行访问。

至少需要分析：

* USB MSC 与 FatFs 同时访问
* 文件系统一致性
* Cache
* DMA
* SDIO 状态
* Block Read / Write
* PC 正在挂载 U 盘时 MCU 是否允许写文件
* PC 拔出 USB 时状态恢复
* USB reset
* SD 卡重新初始化
* MSC eject / mount 行为

如果当前版本为了稳定性采用：

> PC MSC 访问期间禁止 MCU FatFs 主动修改文件系统

允许采用这种策略，但必须明确写入文档和验收测试。

---

# 8. 时钟系统

STM32F429 时钟必须重新设计。

不要复制：

```text
STM32H743 PLL
```

必须针对：

`STM32F429IGT6`

建立：

```text
HSE
 ↓
PLL
 ↓
SYSCLK
 ↓
AHB
 ↓
APB1 / APB2
```

同时满足：

* CPU 正常运行
* USB 48 MHz 时钟准确
* SDIO 时钟符合要求
* SysTick 正常
* HAL Delay 正常

USB 48 MHz 是硬性要求。

如果使用 HSE：

必须根据实际硬件晶振频率设计 PLL。

如果硬件晶振频率未知：

不要猜。

先检查工程 / 板卡资料，并将该参数集中定义。

---

# 9. 工程结构

新工程建议采用：

```text
stm32f429_tinyusb_cdc_msc/
│
├── app/
│   ├── main.c
│   ├── stm32f4xx_hal_conf.h
│   ├── stm32f4xx_it.c
│   ├── syscalls.c
│   └── tusb_config.h
│
├── bsp/
│   ├── bsp.c
│   ├── bsp.h
│   ├── bsp_usb.c
│   ├── bsp_sd.c
│   └── bsp_sd.h
│
├── usb/
│   ├── usb_descriptors.c
│   ├── usb_descriptors.h
│   ├── msc_disk.c
│   └── ...
│
├── Drivers/
│   ├── CMSIS/
│   └── STM32F4xx_HAL_Driver/
│
├── third_party/
│   ├── tinyusb/
│   └── FatFs/
│
├── sys_startup/
│   ├── arm/
│   ├── gcc/
│   ├── stm32f429xx.h
│   ├── stm32f4xx.h
│   ├── system_stm32f4xx.c
│   ├── system_stm32f4xx.h
│   └── stm32f429xx_flash.ld
│
├── MDK-ARM/
│   ├── stm32f429.uvprojx
│   ├── stm32f429.uvoptx
│   └── stm32f429.sct
│
├── cmake/
│   └── arm-none-eabi.cmake
│
├── tools/
│
├── tests/
│   ├── test_cdc.py
│   ├── test_cdc_msc.py
│   └── test_roundtrip.py
│
├── .vscode/
│   ├── launch.json
│   └── tasks.json
│
├── CMakeLists.txt
├── CMakePresets.json
├── openocd.cfg
├── STM32F429.svd
├── build_oneclick.bat
└── README.md
```

---

# 10. sys_startup 要求

不要继续使用 H743 的：

```text
stm32h743xx.h
system_stm32h7xx.c
stm32h743zi_flash.ld
```

必须建立 F429 对应版本。

例如：

```text
sys_startup/
├── gcc/
│   ├── startup_stm32f429xx.s
│   └── ...
│
├── stm32f429xx.h
├── stm32f4xx.h
├── system_stm32f4xx.c
├── system_stm32f4xx.h
└── stm32f429xx_flash.ld
```

必须保证：

* Reset_Handler 正确
* vector table 正确
* SystemInit 正确
* `_estack` 正确
* Flash 地址正确
* RAM 地址正确
* `.data`
* `.bss`
* heap
* stack

均符合 STM32F429IGT6。

---

# 11. 编译环境

必须支持：

```text
arm-none-eabi-gcc
cmake
ninja
python
openocd
arm-none-eabi-gdb
```

要求：

> 工具使用 PATH 中的裸命令，不允许写死绝对路径。

例如：

```text
arm-none-eabi-gcc
cmake
ninja
openocd
arm-none-eabi-gdb
python
```

禁止：

```text
D:/software/...
C:/Program Files/...
```

等绝对路径写入项目配置。

---

# 12. CMake

必须支持：

```bash
cmake --preset debug
cmake --build build/debug
```

以及：

```bash
cmake --preset release
cmake --build build/release
```

Debug：

* `-Og`
* `-g3`
* 调试符号
* 便于 Cortex-Debug

Release：

* `-O2` 或合理优化等级

必须启用：

```text
-Wall
-Wextra
```

项目自身代码要求：

> 0 warning

第三方库可以针对性关闭不适用 warning，但不得全局关闭 warning。

---

# 13. CMake 工程目标

最终生成：

```text
build/debug/f429_tinyusb_cdc_msc.elf
build/debug/f429_tinyusb_cdc_msc.hex
build/debug/f429_tinyusb_cdc_msc.bin
```

并输出：

```text
FLASH usage
RAM usage
```

---

# 14. VSCode 调试

使用：

`Cortex-Debug`

调试器：

`OpenOCD`

接口：

`ST-Link + SWD`

配置必须使用：

```text
${workspaceFolder}
```

禁止绝对路径。

例如：

```text
openocdPath: "openocd"

gdbPath: "arm-none-eabi-gdb"
```

SVD：

```text
${workspaceFolder}/STM32F429.svd
```

OpenOCD：

```text
${workspaceFolder}/openocd.cfg
```

---

# 15. OpenOCD

建立：

```text
openocd.cfg
```

目标：

```text
ST-Link
+
SWD
+
STM32F4
```

初始 SWD 频率采用安全值：

```text
1800 kHz
```

确认稳定后再允许提高。

禁止为了追求下载速度直接设置非常高的 SWD 频率。

---

# 16. VSCode F5 调试流程

F5：

```text
Build
 ↓
OpenOCD
 ↓
ST-Link
 ↓
SWD
 ↓
Reset
 ↓
Load ELF
 ↓
Halt
 ↓
进入 main()
```

必须支持：

* breakpoint
* step over
* step into
* continue
* call stack
* registers
* variables
* memory
* peripheral registers

---

# 17. MDK-ARM

同时提供：

```text
MDK-ARM/stm32f429.uvprojx
```

要求：

* Keil MDK-ARM 可以打开
* 可以编译
* 可以下载
* 可以调试
* 使用 SWD
* 与 GCC/CMake 工程使用相同源代码

Keil 与 CMake 不允许维护两套业务逻辑代码。

---

# 18. STM32F429 SVD

必须使用 STM32F429 对应 SVD：

```text
STM32F429.svd
```

不要继续使用：

```text
STM32H743.svd
```

---

# 19. TinyUSB 移植要求

必须分析 H743 工程中的：

```text
tinyusb/src/
```

特别关注：

```text
portable/
```

STM32F429 与 STM32H743 不允许直接假设 USB 底层实现完全一致。

必须确认 TinyUSB 对 STM32F4 / Synopsys DWC2 的支持方式。

重点检查：

* USB controller
* DWC2
* FIFO
* endpoint
* interrupt
* device mode
* FS PHY
* USB clock
* USB reset
* endpoint allocation

如果 TinyUSB 自动选择了：

```text
synopsys/dwc2
```

必须确认这是 F429 实际 USB IP 的正确路径。

---

# 20. USB Descriptor

建立：

```text
usb_descriptors.c
```

实现：

```text
CDC
+
MSC
```

Composite Device。

必须检查：

* VID/PID
* Manufacturer
* Product
* Serial Number
* Interface Number
* Endpoint Number
* Endpoint Address
* Max Packet Size
* Configuration Descriptor
* String Descriptor
* CDC Interface
* MSC Interface

不要机械复制 H743 的 Descriptor。

---

# 21. CDC 测试

必须实现 PC 端 Python 测试：

```text
tests/test_cdc.py
```

至少验证：

```text
PC -> USB CDC -> MCU -> USB CDC -> PC
```

测试：

* 短包
* 64B
* 256B
* 1KB
* 4KB
* 连续发送
* 连续接收
* Echo
* 多次重复测试

---

# 22. CDC 长数据测试

特别测试：

```text
连续 1 MB
```

或合理大小的数据。

检查：

* 数据丢失
* 数据重复
* 数据错位
* FIFO overflow
* endpoint overflow
* application buffer overflow

测试必须计算：

```text
TX bytes
RX bytes
CRC / checksum
error count
```

不能只依赖“串口看起来正常”。

---

# 23. MSC 测试

PC 插入 USB 后：

必须：

```text
枚举成功
↓
出现 U 盘
↓
可以读取
↓
可以写入
↓
可以创建文件
↓
可以删除文件
↓
重新插拔后文件仍然存在
```

---

# 24. MSC 文件系统

FatFs 必须能够：

```text
mount
open
read
write
close
```

测试：

```text
TEST.TXT
```

写入：

```text
Hello STM32F429 TinyUSB MSC
```

然后：

```text
f_open
f_read
```

确认内容一致。

---

# 25. CDC + MSC 并发测试

这是最终核心测试。

PC 同时执行：

```text
USB CDC 连续数据通信
```

以及：

```text
USB MSC 文件读写
```

同时进行。

验证：

```text
CDC 不丢数据
MSC 不损坏
SD 卡文件系统正常
USB 不掉线
MCU 不 HardFault
MCU 不死锁
```

---

# 26. 重要的 F429 风险检查

在开发过程中必须主动检查以下问题。

## 风险 1：USB 48 MHz

必须确认：

```text
USB clock = 48 MHz
```

否则 USB 可能无法稳定枚举。

---

## 风险 2：SDIO DMA

检查：

* DMA Stream
* DMA Channel
* IRQ
* DMA buffer
* cache

F429 没有 H7 那种 D-Cache，因此：

> 不要把 H743 的 Cache/DMA 处理逻辑机械复制过来。

---

## 风险 3：SRAM

重新计算：

```text
RAM
stack
heap
USB buffers
MSC buffers
FatFs buffers
```

不要复制 H743 的 linker script。

---

## 风险 4：USB FIFO

检查：

```text
RX FIFO
TX FIFO
EP FIFO
```

确认 TinyUSB 配置与 F429 USB OTG FS FIFO RAM 匹配。

---

## 风险 5：SD 卡与 MSC 同时访问

这是最高优先级风险之一。

必须设计：

```text
SD access arbitration
```

不能让：

```text
FatFs
```

和：

```text
MSC
```

同时随意操作 SD 卡。

---

## 风险 6：USB Reset

测试：

```text
PC USB reset
```

以及：

```text
拔插 USB
```

确认：

* USB stack 恢复
* MSC 恢复
* CDC 恢复
* SD 卡状态不异常

---

## 风险 7：SD 卡拔插

如果硬件支持 SD 卡检测：

必须测试：

```text
SD inserted
SD removed
SD reinserted
```

如果不支持：

必须明确记录：

> 当前版本不支持运行时 SD 卡热插拔。

---

## 风险 8：USB Suspend / Resume

测试：

```text
PC 睡眠
PC 唤醒
USB Suspend
USB Resume
```

确认 USB 不死锁。

---

# 27. 开发顺序

不要一次性实现所有功能。

严格按照：

## Phase 1

STM32F429 最小工程。

验证：

```text
Reset
 ↓
SystemInit
 ↓
main
 ↓
LED
```

---

## Phase 2

加入：

```text
USB Device
```

只实现 CDC。

验证：

```text
USB enumeration
CDC COM Port
Echo
```

---

## Phase 3

加入：

```text
SDIO
+
FatFs
```

验证：

```text
mount
read
write
```

---

## Phase 4

加入：

```text
TinyUSB MSC
```

验证：

```text
PC U Disk
```

---

## Phase 5

实现：

```text
CDC + MSC Composite
```

验证两个 Interface 同时枚举。

---

## Phase 6

并发测试：

```text
CDC traffic
+
MSC traffic
```

---

## Phase 7

异常测试：

```text
USB reset
USB reconnect
PC suspend/resume
SD card error
SD card reconnect
long CDC traffic
large file transfer
```

---

# 28. 自动化测试

建立：

```text
tests/
```

至少：

```text
test_cdc.py
test_msc.py
test_cdc_msc.py
test_roundtrip.py
```

测试结果应该明确输出：

```text
PASS
FAIL
```

例如：

```text
CDC Echo Test
  64B       PASS
  256B      PASS
  1KB       PASS
  4KB       PASS
  1MB       PASS

MSC Test
  Mount     PASS
  Read      PASS
  Write     PASS
  Delete    PASS

CDC + MSC Concurrent
  CDC       PASS
  MSC       PASS
  Stability PASS
```

---

# 29. 一键编译

提供：

```text
build_oneclick.bat
```

执行：

```text
check tools
check dependencies
cmake configure
clean
build
size
```

如果工具缺失：

明确提示：

```text
Missing dependency: xxx
```

不要静默失败。

---

# 30. 禁止事项

禁止：

1. 直接修改 H743 原工程
2. 直接复制 H743 linker script
3. 直接复制 H743 startup
4. 继续使用 STM32H7 HAL
5. 使用 H743 SVD
6. 使用 H743 device header
7. 使用绝对路径
8. 修改第三方 TinyUSB 源码来“强行适配”
9. 为了编译通过关闭大量 warning
10. 没有硬件验证就宣布 USB 移植完成
11. 只测试 USB 枚举，不测试 CDC 实际传输
12. 只测试 MSC 枚举，不测试实际文件读写
13. 不测试 CDC + MSC 并发
14. 假设 SDIO 与 FatFs/MSC 可以无锁并发
15. 遇到错误直接修改代码而不分析根因

---

# 31. Agent 工作方式

你不是简单执行代码生成。

必须按照：

```text
分析
 ↓
制定迁移方案
 ↓
检查 F429 硬件资源
 ↓
建立最小工程
 ↓
编译
 ↓
烧录
 ↓
硬件验证
 ↓
增加功能
 ↓
测试
 ↓
记录问题
 ↓
修复
 ↓
重新验证
```

每一个阶段完成后必须给出：

```text
已完成
未完成
发现的问题
验证方法
下一步
```

---

# 32. 遇到 H743 与 F429 差异时

不要问：

> “H743 是怎么实现的？”

而应该问：

> “这个功能在 F429 上应该如何正确实现？”

参考工程只提供：

```text
架构
功能
接口
测试方法
代码组织
```

不提供：

```text
芯片寄存器实现
时钟配置
DMA 配置
启动文件
linker script
USB 底层假设
```

这些必须针对 F429 重新设计。

---

# 33. 最终验收标准

只有以下全部通过，才允许宣布：

```text
STM32F429 TinyUSB CDC+MSC 移植完成
```

### 编译

* [ ] GCC 编译成功
* [ ] CMake Debug 成功
* [ ] CMake Release 成功
* [ ] 0 warning
* [ ] ELF/HEX/BIN 正常生成

### MDK

* [ ] Keil 打开工程成功
* [ ] 编译成功
* [ ] 下载成功
* [ ] SWD 调试成功

### OpenOCD

* [ ] ST-Link 正常识别
* [ ] SWD 正常
* [ ] reset 正常
* [ ] halt 正常
* [ ] GDB 正常

### USB

* [ ] USB 枚举成功
* [ ] CDC 枚举成功
* [ ] MSC 枚举成功
* [ ] CDC + MSC 同时出现

### CDC

* [ ] Echo
* [ ] 64B
* [ ] 256B
* [ ] 1KB
* [ ] 4KB
* [ ] 长时间连续传输
* [ ] 1MB 数据校验

### MSC

* [ ] U 盘出现
* [ ] 文件读取
* [ ] 文件写入
* [ ] 文件创建
* [ ] 文件删除
* [ ] 重新插拔后数据保持

### 并发

* [ ] CDC + MSC 同时工作
* [ ] CDC 无丢包
* [ ] MSC 无文件损坏
* [ ] 无 USB 掉线
* [ ] 无 HardFault
* [ ] 无死锁

### 异常

* [ ] USB reset
* [ ] USB disconnect/reconnect
* [ ] PC suspend/resume
* [ ] SD 卡异常
* [ ] 长时间运行

---

# 34. 最终输出文档

完成后更新：

```text
README.md
```

至少包含：

1. 项目介绍
2. 硬件接口
3. 工程结构
4. 编译方法
5. 烧录方法
6. VSCode 调试
7. Keil 调试
8. USB CDC 使用
9. USB MSC 使用
10. SD 卡说明
11. 并发访问策略
12. PC 测试方法
13. 已知限制
14. 验收结果

---

# 35. 最终原则

本项目不是：

> “把 STM32H743 改成 STM32F429。”

而是：

> “参考 H743 项目的架构和功能，在 STM32F429IGT6 上重新实现一个正确、独立、可编译、可调试、可测试的 TinyUSB CDC + MSC 工程。”

优先保证：

```text
正确性
>
可验证性
>
工程结构
>
可维护性
>
性能
>
代码复用
```

如果参考工程中的实现与 STM32F429 的硬件架构存在冲突：

> 以 STM32F429 数据手册、参考手册、CMSIS/HAL、TinyUSB 对 F4 的正确实现为准，而不是强行保持与 H743 代码一致。
