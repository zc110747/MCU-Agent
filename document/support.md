# 单片机支持ai agent开发环境

对于单片机来说，开发工具五花八门，如keil、iar、stm32cubeIDE等，不过对于AI Agent最友好的还是如下所示。

- `arm-xxx-gcc + cmake + ninja + openocd + stlink/jlink`仿真的方案。

只要系统中存在这些工具，Agent可以直接在windows下构建环境。

1. 编译工具：arm-none-eabi-gcc，下载地址: 