# 单片机支持ai agent开发环境

对于单片机来说，开发工具五花八门，如keil、iar、stm32cubeIDE等，不过对于AI Agent最友好的还是如下所示。

❗ `arm-xxx-gcc + cmake + ninja + openocd + stlink/jlink`仿真的方案。  
❗ 需要支持Python用于生成测试代码，执行修改、测试任务。  

可使用如下语句，在agent中一键安装。

```shell
项目添加嵌入式在线仿真调试安装环境，具体如下。

1. 工具内容python、vscode(Cortex-Debug)、arm-none-eabi-gcc、cmake、ninja、openocd，且需要添加到系统环境中
2. 命令行测试直接指令访问检查当前环境，不存在则安装到D:/software/embed-tools中，存在则不需要安装
3. 完成后通过命令行测试，并告知全部结果
```

只要系统中存在这些工具，Agent可以直接在windows下构建环境。

⚠️注意：**开发环境程序都需要添加到系统环境变量中，可以直接在命令行访问，才能后续被项目使用。**

⚠️注意: **可将本信息告知AI Agent, 让其配置好相关环境并测试**

⚠️注意: **相关路径可能随着网站更新而变化，需要自己判断修改**

⚠️注意: **stlink、jlink、usb转串口驱动需要自行安装**

## 支持工具地址

🔧 **编译工具：arm-none-eabi-gcc**  

- 🖥️ 下载地址: <https://gitlab.arm.com/tooling/gnu-toolchains-for-arm/-/tree/releases/15.3.rel1?ref_type=heads>

🔧 **单片机调试工具: openocd**  

- 🖥️ 下载地址: <https://sourceforge.net/projects/openocd/files/openocd/0.12.0-rc1/>

🔧 **在线仿真工具: vscode + Cortex-Debug**  

- 🖥️ 下载地址：<https://code.visualstudio.com/?wt.mc_id=DX_841432>
- 插件直接在商店搜索下载

🔧 **编译工具: cmake、ninja**  

windows平台使用MSYS2安装，安装方法如下所示。  

- 🖥️ MYS2安装: <https://www.msys2.org/?utm_source=chatgpt.com>

执行如下安装命令：

```shell
pacman -Syu

# 安装基础工具链
pacman -S --needed \
base-devel \
git \
wget \
vim

# 安装gcc工具链
pacman -S mingw-w64-ucrt-x86_64-gcc

# 安装cmake
pacman -S mingw-w64-ucrt-x86_64-cmake

# 安装ninga
pacman -S mingw-w64-ucrt-x86_64-ninja
```

🔧 **测试工具：Python**  

- 🖥️ 下载地址：<https://www.python.org/downloads/release/python-3147/>
