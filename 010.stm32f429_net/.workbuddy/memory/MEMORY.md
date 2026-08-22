# 项目长期笔记：010.stm32f429_net

## 工程定位
STM32F429IGT6 **FreeRTOS V11.1.0 + LwIP 2.1 多线程** 网络工程 + 内嵌 HTTP server（netconn API）+ 外部 SDRAM 内存池。
cmake + ninja 构建，arm-none-eabi-gcc 编译，openocd + ST-Link 调试，VSCode Cortex-Debug 单步。

## 硬件事实（已实测确认，勿凭推断修改）
- **连续 SRAM 仅 192K**：0x20000000~0x2002FFFF（SRAM1 112K + SRAM2 16K + SRAM3 64K）
  `_estack = 0x20030000`。CCM 64K @ 0x10000000 不连续，**ETH DMA 访问不到**，勿放网络缓冲。
- **FLASH 1024K** @ 0x08000000（openocd 报 device id 0x20016419, Single Bank）
- **SDRAM：W9825G6KH-6, 32MB @ 0xC0000000**（FMC Bank1, 16bit, 90MHz, 13行×9列×4bank）
- LAN8720A PHY，RMII，地址 0；ETH_RESET 由 PCF8574T(I2C addr 0x20) P7 控制
  （P7=1 → RESET 拉低复位；P7=0 → 正常）
- USART1 = PA9/PA10（COM3）；LED = PB0/PB1 低电平点亮；HSE 25MHz（PLL M=25 N=360 P=2 Q=8 → 180MHz）
- 网络：静态 IP 192.168.10.99 / 掩码 255.255.255.0 / 网关 192.168.10.1
  开发主机以太网口 192.168.10.5，同网段可直接 ping

## 架构约定
- `app/` 应用逻辑（main、FreeRTOSConfig、lwipopts.h、hal_conf、syscalls、lwip/、web/、sdram_heap.c）
- `bsp/` 板级驱动（bsp_uart、bsp_i2c、bsp_sdram…）
- `Drivers/` ST HAL + CMSIS（startup 向量表 SVC/PendSV/SysTick 已直指 FreeRTOS port 函数）
- `third_party/` LwIP 2.1（NO_SYS=0 多线程，编译 api/ 层 + sys_arch.c）、FreeRTOS-Kernel V11.1.0
- 调试配置一律用相对路径，不写绝对路径

## 内存布局（关键）
- **ETH RX 零拷贝缓冲（RX_POOL）必须留在内部 SRAM**：ETH DMA 写 SDRAM 在分片突发时丢包
  （实测 `ping -l 1473` 起不稳定；RX_POOL 移回 SRAM 后 32B~16000B 全通）
- LwIP mem heap（ram_heap, MEM_SIZE=128KB）在 SDRAM @0xC0000000
- FreeRTOS heap（ucHeap, 512KB）在 SDRAM @0xC0020000（链接脚本 .freertos_heap NOLOAD 段）
- memp 池（PBUF 等）在 SRAM（memp.h 不加 section 属性）

## 必须记住的硬约束
1. **校验和开关必须两处同步**：`app/lwipopts.h` 的 `ETH_USE_HW_CHECKSUM` 同时驱动
   lwipopts 的 CHECKSUM_* 和 `ethernetif.c` 的 `TxConfig.ChecksumCtrl`。
   当前默认软件校验和（=0），因为硬件卸载会破坏 IP 分片。
2. **HAL 时基 = TIM7**（SysTick 让给 FreeRTOS）：`stm32f4xx_hal_timebase_tim.c`。
   `HAL_InitTick` 里 EnableIRQ 无条件执行 + Start_IT 前 Stop_IT 重置 State（重复调用坑）。
3. **FreeRTOS V11 移植三个坑**：
   - `INCLUDE_vTaskDelay` 等默认 0，必须显式开启
   - port.c 需 `-mfloat-abi=softfp -mfpu=fpv4-sp-d16`（PendSV 汇编有 FPU 指令）
   - startup 向量表 SVC/PendSV/SysTick 必须**直接** `.word vPortSVCHandler/xPortPendSVHandler/xPortSysTickHandler`
     （V11 port.c assert 检查，不能转发包装）
4. **调度器启动前 xTaskCreate 残留 BASEPRI=0x50**：vPortExitCritical 只在调度器运行时恢复中断。
   `tcpip_init()` 后必须 `__set_BASEPRI(0); __enable_irq();`，否则 TIM7/HAL_Delay 卡死。
5. **SYS_ARCH_PROTECT 不能在 ISR 调 vPortEnterCritical**：sys_arch.c 用 `xPortIsInsideInterrupt()`
   跳过临界区（ETH ISR → memp_malloc 路径）。
6. **netconn close 前必须读完请求**（未读数据 close → RST）。

## 构建/调试要点
- 改链接脚本必须靠 `LINK_DEPENDS` 才会触发重链接，否则 ninja 报 no work to do
- openocd 用 `transport select swd`（sysprogs 0.12.0 不支持 hla_swd）
- 编译期宏体检：`arm-none-eabi-gcc -dM -E`（`#pragma message` 在 -E 下不求值，不可用）
- 反复用 openocd/gdb 后 USB 会被残留进程占用（LIBUSB_ERROR_ACCESS），先 `Get-Process openocd | Stop-Process -Force`

## 环境注意
- Git Bash 会吞 curl 的 `%{...}` 花括号 → 用响应体内容判定，别用 `-w "%{http_code}"`
- 原生工具链看不到 MSYS 的 `/tmp`、`/dev/null`，临时文件放项目目录内
- Windows 命令中文输出需 `iconv -f GBK -t UTF-8`
- mbedTLS 3.x（后续 TLS）：`MBEDTLS_CONFIG_FILE=<mbedtls_config.h>` 用尖括号；
  PEM parse 传 `strlen+1`；`MBEDTLS_MEMORY_BUFFER_ALLOC_C` 需配 `MBEDTLS_PLATFORM_MEMORY`
