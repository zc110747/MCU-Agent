# 基于tinyusb实现cmsis-dap协议项目

构建STM32平台的vscode项目，基于第三方库tinyusb实现cmsis-dap v1协议项目，能够仿真stm32f4。  

项目生成后直接构建，仿真，异常时使用st-link进行单步调试项目，并连接stm32f429，使用openocd进行验证

## 硬件资源(详细原理图和部分驱动见附件)

1. 芯片STM32H743ZIT6，USB为FS模式，外部无源晶振25MHZ
2. USB硬件接口为FS，已经连接，可直接调试
3. 使用硬件引脚模拟cmsis-dap调试接口，引脚连接如下(支持JTAG/SWD接口)：

- JTRST - PA3
- JTDI - PC4
- JTMS/SWDIO - PA0
- JTCK/SWCLK - PA1
- JTDO - PA7
- NRST - PA2

## 编译环境

1. 使用cmake+ninja管理项目
2. 编译工具arm-none-gnueabi-gcc
3. 仿真环境openocd + stlink
4. vscode已经集成Cortex-Debug，可以实现单步仿真，允许仿真调用
5. python已经安装，允许直接调用
6. 上述程序已经添加到系统环境中，生成调试文件时不要使用绝对路径

## 工程模块化

对于部分目录给出结构，项目额外下载要尽可能精简，下载必须即可

1. app: 存放应用逻辑
2. bsp：存放用户开发的驱动程序
3. Drivers: HAL相关放在Drivers目录下
4. third_party: 存放第三方库(如FatFs、lvgl)

## 项目注意

1. 协议需要兼容支持`CMSIS-DAP v1`即可，考虑兼容性
2. 提供ARM的CMSIS-DAP、JTAG/SWD接口的源码，详细见附件

## 建议流程

1. 先基于USB实现cmsis-dap协议设备，并能够支持openocd的命令仿真
2. 实现引脚模拟实现cmsis-dap协议的所有时序，结合命令仿真，实现对芯片的访问
3. 两部分组合，最终实现完整功能
4. 总结具体流程和问题解决办法，更新到项目下的README.md文件中

## 验收标准

1. 使用openocd能够正常通过此单片机，访问stm32f429的芯片，支持下载和调试命令

## 附件

原理图.pdf、JTAG_DP.c、SW_DP.c、SWO.c、DAP.h
