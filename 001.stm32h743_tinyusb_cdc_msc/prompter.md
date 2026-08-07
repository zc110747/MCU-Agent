# 基于tinyusb实现USB混合设备(CDC+MSC)

构建STM32平台的vscode项目，基于第三方库tinyusb实现CDC+MSC混合设备。  

项目生成后直接构建，仿真，异常时使用st-link进行单步调试解决。

- 硬件资源(详细原理图见附件，直接解析补充)

1. 芯片STM32H743ZIT6，USB为HS模式，外部无源晶振25MHZ
2. USB硬件已经连接，可直接调试
3. 外部已经接入SD卡，可直接测试

- 编译环境

1. 使用cmake+ninja管理项目
2. 编译工具arm-none-gnueabi-gcc
3. 仿真环境openocd + stlink
4. vscode已经集成Cortex-Debug，可以实现单步仿真，允许仿真调用
5. python已经安装，允许直接调用
6. 上述程序已经添加到系统环境中，生成调试文件时不要使用绝对路径

- 工程模块化

对于部分目录给出结构，项目额外下载要尽可能精简，下载必须即可

1. app: 存放应用逻辑
2. bsp：存放用户开发的驱动程序
3. Drivers: HAL相关放在Drivers目录下
4. third_party: 存放第三方库(如FatFs、lvgl)

- 建议流程

1. 先实现CDC设备，测试串口通讯正常
2. 再实现混合设备

- 验收标准

1. 串口测试能够正常通讯，系统能够实现U盘访问
2. 两个设备能够同时工作，互不干扰

## 附件

原理图.pdf
