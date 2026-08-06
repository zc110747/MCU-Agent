# 提示词说明

## 001.基于tinyusb实现USB混合设备(CDC+MSC)

```shell
构建STM32平台的vscode项目，基于tinyusb实现CDC+MSC混合设备

1. 芯片型号为STM32H743ZIT6(鹿小班开发板)，原理图见附件，USB使用FS模式
2. 编译工具使用arm-none-gnueabi-gcc，使用CMake管理项目，仿真工具使用ST-Link，相关程序已经添加到系统中，不要使用绝对路径   
3. vscode环境已经集成cortex-debug，可以实现单步仿真 
4. HAL相关放在Drivers目录下，第三方库(如Fatfs、lvgl、tinyusb)放在third_party，方便后续统一管理  
5. USB硬件已经连接，代码实现完成后，直接进行仿真调试。

附件:
原理图.pdf
```

## 002.基于tinyusb实现UVC摄像头(基于ov5640)

```shell
构建STM32平台的vscode项目，基于tinyusb实现UVC摄像头应用

1. 芯片型号为STM32H743ZIT6(鹿小班开发板)，原理图见附件，提供部分摄像头ov5640驱动，USB使用FS模式
2. 编译工具使用arm-none-gnueabi-gcc，使用CMake管理项目，仿真工具使用ST-Link，相关程序已经添加到系统中，不要使用绝对路径   
3. vscode环境已经集成cortex-debug，可以实现单步仿真 
4. 图像满足240x240即可，使用0x24000000作为缓冲，支持双或三缓冲
5. HAL相关放在Drivers目录下，第三方库(如Fatfs、lvgl、tinyusb)放在third_party，方便后续统一管理  
6. USB硬件已经连接，代码实现完成后，直接进行仿真调试。

附件:
原理图.pdf、drv_dcmi.c、drv_dcmi.h、drv_dcmi_ov5640.c、drv_dcmi_ov5640.h
```

## 003.使用lvgl实现oled显示(支持sd卡解析中文字库)

```shell
构建STM32平台的vscode项目，基于lvgl实现oled界面显示

1. 芯片型号为STM32H743ZIT6(鹿小班开发板)，原理图见附件，提供部分已经调试的OLED驱动    
2. 编译工具使用arm-none-gnueabi-gcc，使用CMake管理项目，仿真工具使用ST-Link，相关程序已经添加到系统中，不要使用绝对路径   
3. vscode环境已经集成cortex-debug，可以实现单步仿真  
4. HAL相关放在Drivers目录下，第三方库(如Fatfs、lvgl)放在third_party，方便后续统一管理  
5. 中文字库位于SD卡中的/SYSTEM/FONT/下
6. oled屏幕尺寸240x240
7. 代码实现完成后，直接进行仿真调试。

附件:
原理图.pdf、drv_spi_oled.c、drv_spi_oled.h
```

## 004.实现从sd卡中读取图片并使用oled轮播

```shell
构建STM32平台的vscode项目，实现从SD卡读取图片，周期更新显示到OLED上

1. 芯片型号为STM32H743ZIT6(鹿小班开发板)，原理图见附件，提供部分已经调试的OLED驱动    
2. 编译工具使用arm-none-gnueabi-gcc，使用CMake管理项目，仿真工具使用ST-Link，相关程序已经添加到系统中，不要使用绝对路径   
3. vscode环境已经集成cortex-debug，可以实现单步仿真  
4. HAL相关放在Drivers目录下，第三方库(如Fatfs、lvgl)放在third_party，方便后续统一管理  
5. 图片位于SD卡中的image目录下，格式为jpg，5s周期更新  
6. oled屏幕尺寸240x240，图像可进行先裁剪后缩放来满足尺寸显示
7. 代码实现完成后，直接进行仿真调试。

附件:
原理图.pdf、drv_spi_oled.c、drv_spi_oled.h
```
