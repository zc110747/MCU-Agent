# 基于zephyr和lvgl实现OLED显示功能

构建STM32平台的vscode项目，基于SD卡驱动、中文GBK字库、第三方库zephyr和lvgl实现OLED显示功能

项目生成后直接构建，仿真，异常时使用st-link进行单步调试解决。

## 硬件资源(详细原理图见附件，直接解析补充)

1. 芯片STM32H743ZIT6，外部无源晶振25MHZ
2. OLED硬件已经连接，尺寸240x240，可直接调试，可提供部分已验证驱动代码
3. 外部已经接入SD卡，其中中文字库位于"1:/SYSTEM/FONT/"目录，可提供部分已验证驱动代码
4. 支持调试串口，连接PA9/PA10，用于zephyr的调试接口
5. LED用于验证系统是否正常工作

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
4. third_party: 存放第三方库(如FatFs、lvgl、zephyr)

其它目录无限制。

## 建议流程

1. 移植zephyr作为基础的RTOS应用，构建任务执行
2. 实现SD卡数据读取
3. 再实现OLED的LED显示输出
4. 集成LVGL，实现基于LVGL的显示应用
5. 完成后总结项目执行步骤、解决问题，更新到项目下的README.md文件中

## 注意

1. 只在workspace目录下工作，不要影响其它目录
2. 硬件环境已经搭建，可以直接仿真使用

## 验收标准

1. 实机调试仿真，确定在OLED上显示界面符合预期

## 附件

注意: 驱动是裸机版本，自行移植支持zephyr，RTOS已经支持直接使用即可
drv_oled_fonts.cdrv_oled_fonts.hdrv_oled_text.cdrv_oled_text.hdisk_interface.cdisk_interface.hdrv_sdio.cdrv_sdio.hdrv_spi_oled.cdrv_spi_oled.h# 基于zephyr和lvgl实现OLED显示功能

