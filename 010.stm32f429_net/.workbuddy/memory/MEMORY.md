# 项目长期笔记：010.stm32f429_net

## 工程定位
STM32F429IGT6 裸机 LwIP (NO_SYS) 网络工程 + 内嵌 HTTP server。
cmake + ninja 构建，arm-none-eabi-gcc 编译，openocd + ST-Link 调试，VSCode Cortex-Debug 单步。

## 硬件事实（已实测确认，勿凭推断修改）
- **连续 SRAM 仅 192K**：0x20000000~0x2002FFFF（SRAM1 112K + SRAM2 16K + SRAM3 64K）
  `_estack = 0x20030000`。CCM 64K @ 0x10000000 不连续，**ETH DMA 访问不到**，勿放网络缓冲。
- **FLASH 1024K** @ 0x08000000（openocd 报 device id 0x20016419, Single Bank）
- LAN8720A PHY，RMII，地址 0；ETH_RESET 由 PCF8574T(I2C addr 0x20) P7 控制
  （P7=1 → RESET 拉低复位；P7=0 → 正常）
- USART1 = PA9/PA10（COM3）；LED = PB0/PB1 低电平点亮；HSE 25MHz（PLL M=25 N=360 P=2 Q=8 → 180MHz）
- 网络：静态 IP 192.168.10.99 / 掩码 255.255.255.0 / 网关 192.168.10.1
  开发主机以太网口 192.168.10.5，同网段可直接 ping

## 架构约定
- `app/` 应用逻辑（main、lwipopts.h、hal_conf、syscalls、lwip/、web/）
- `bsp/` 板级驱动（bsp_uart、bsp_i2c…）
- `Drivers/` ST HAL + CMSIS
- `third_party/` LwIP 2.1（只编译 raw API，排除 api/ apps/ ppp/ ipv6/ core/sys.c 等）
- 调试配置一律用相对路径，不写绝对路径

## 必须记住的两条硬约束
1. **校验和开关必须两处同步**：`app/lwipopts.h` 的 `ETH_USE_HW_CHECKSUM` 同时驱动
   lwipopts 的 CHECKSUM_* 和 `ethernetif.c` 的 `TxConfig.ChecksumCtrl`。
   若 lwIP 算了校验和、MAC 又插入一次，MAC 会覆盖正确值 → 连不分片的包也坏。
   当前默认软件校验和（=0），因为硬件卸载会破坏 IP 分片（详见 2026-08-22 日志）。
2. **LwIP NO_SYS 移植的头文件策略**：`app/lwipopts.h` 设 `NO_SYS 1`，使
   `lwip/sys.h` 永不 include RTOS 专用的 `system/arch/sys_arch.h`；
   `app/lwip/arch/cc.h` 遮蔽 third_party 版本（避开 newlib-nano 的 `<sys/time.h>` 问题）。
   include 路径顺序必须 `app/lwip` 在 `third_party/LwIP/system/arch` 之前。
   `SYS_ARCH_PROTECT` 用内联汇编宏实现（PRIMASK 存取），避免引入 sys_arch.c。

## 构建/调试要点
- 改链接脚本必须靠 `LINK_DEPENDS` 才会触发重链接，否则 ninja 报 no work to do
- openocd 用 `transport select swd`（sysprogs 0.12.0 不支持 hla_swd）
- 编译期宏体检：`arm-none-eabi-gcc -dM -E`（`#pragma message` 在 -E 下不求值，不可用）

## 环境注意
- Git Bash 会吞 curl 的 `%{...}` 花括号 → 用响应体内容判定，别用 `-w "%{http_code}"`
- 原生工具链看不到 MSYS 的 `/tmp`、`/dev/null`，临时文件放项目目录内
- Windows 命令中文输出需 `iconv -f GBK -t UTF-8`
