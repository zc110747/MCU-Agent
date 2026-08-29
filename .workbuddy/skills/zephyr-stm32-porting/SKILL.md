---
name: zephyr-stm32-porting
description: STM32 + Zephyr RTOS + LVGL 的移植实战：west 工作区与模块体系、设备树 overlay（SPI/MIPI-DBI/SDMMC/USART/LED）、25MHz HSE 时钟树覆盖、SDMMC/FatFs 卷名、GBK 中文点阵字库双段结构渲染、ST7789 DISPON patch、Zephyr shell 命令控制台、LVGL 内存池与 tick 来源。适用于"在 STM32 上跑 Zephyr""Zephyr 移植 LVGL""Zephyr 中文显示""设备树配置 ST7789/SD 卡""west build 调试"。触发词：Zephyr、west、STM32 Zephyr、LVGL Zephyr、设备树 overlay、nucleo_h743zi、MIPI-DBI、SDMMC、Zephyr shell、GBK 字库 Zephyr、ST7789 Zephyr、时钟树 25MHz、LVGL 内存池、CMakeLists 工具链自动探测 ZEPHYR_TOOLCHAIN_VARIANT、west build -b 全新构建、build_oneclick if exist build、PowerShell west stderr 误报。
agent_created: true
---

# Zephyr + STM32 + LVGL 移植实战

基于真机项目：STM32H743ZIT6（目标板 `nucleo_h743zi`）+ Zephyr 3.7.0 + LVGL 8.4.0 +
ST7789 SPI6 OLED + SD 卡 GBK 字库。与裸机工程（见 `stm32-peripheral-drivers`）的差异点
是本 skill 重点。配套：`stm32-ai-dev-environment`（west 调用）、`stm32-project-scaffold`（结构）。

## 一、west 工作区与模块体系

```
west.yml                  # manifest：zephyr 3.7.0 + hal_stm32 + cmsis + fatfs + lvgl
CMakeLists.txt            # 应用构建（app + bsp，链接 LVGL）
prj.conf                  # Zephyr / LVGL / FatFs / 显示配置
boards/nucleo_h743zi.overlay   # 设备树覆盖
tools/openocd.cfg         # ST-Link SWD（adapter speed 4000）
app/  main.c ui.c shell_cmds.c
bsp/  drv_oled_fonts.* drv_oled_text.* lv_port_font.* ascii_*.c
```

**west 调用**（若 `west` 不在 PATH，必须 python 模块）：
```bash
python -m west build -b <board>/<soc> -d build -s .
```
- `ZEPHYR_BASE` 优先取环境变量（west 自动注入）；未设回退工程内 `zephyr/zephyr`。
- MSYS2 下手动 export `ZEPHYR_BASE` 用 Windows 风格绝对路径（如 `C:/...`），非 POSIX 风格（`/c/...`）。

## 二、设备树 overlay 要点

### 2.1 时钟树：覆盖 25MHz HSE
目标板 Nucleo 是 8MHz ST-Link 旁路时钟，用户板是 **25MHz 无源晶振**，必须覆盖：
```dts
&clk_hse { clock-frequency = <DT_FREQ_M(25)>; };   // 去 hse-bypass
&pll { div-m = <5>; mul-n = <192>; };              // VCO 960MHz, SYSCLK 480MHz
```
SDMMC 时钟源 PLL1Q 须 48MHz（否则 `f_mount` 返回 `FR_NOT_READY`）：
```dts
&pll { div-q = <20>; }          // 960/20 = 48 MHz
&sdmmc1 { bus-width = <4>; status = "okay"; };   // + idma
```

### 2.2 ST7789 (MIPI-DBI / SPI6)
```dts
&spi6 {
  status = "okay";
  st7789v: st7789v@0 {
    compatible = "sitronix,st7789v";
    spi-max-frequency = <DT_FREQ_M(40)>;
    reg = <0>; cmd-data-gpios = <&gpiog 15 GPIO_ACTIVE_HIGH>;  // DC
    /* ... width/height/rotation ... */
  };
};
```
- OLED SCK=PG13, MOSI=PG14, CS=PG8, DC=PG15, BL=PG12（见 `stm32-peripheral-drivers` §一）。

### 2.3 SDMMC1 + FatFs 卷名
Zephyr FatFs 用**字符串卷名**：`FF_STR_VOLUME_ID=1`，`CONFIG_SDMMC_VOLUME_NAME="SD"`，
逻辑盘符必须 `SD:`（裸机用 `1:`，见 `stm32-peripheral-drivers` §5.4）。

### 2.4 USART1 调试串口 + shell
```dts
&usart1 { status = "okay"; current-speed = <115200>; };
chosen { zephyr,shell-uart = &usart1; };
```

## 三、ST7789 显示开启（关键差异）

Zephyr 的 st7789v 驱动初始化**不发 DISPON(0x29)**，面板保持 sleep-in 全黑。
应用必须显式点亮：
```c
display_blanking_off(display_dev);   // app/main.c 步骤 1b
```
**软件复位延时不足**：用户板无硬件 RST，Zephyr 走 SWRESET 后仅延 5ms（远小于 ST7789
复位 ~120ms），初始化命令（含 SLPOUT）被忽略 → 全黑。需 patch `display_st7789v.c`：
```c
k_sleep(K_MSEC(120));   // SWRESET 后，对齐裸机 10ms 后配置
```
⚠️ patch 在 `west update` 后可能被覆盖，重装模块需重新应用。

## 四、GBK 中文点阵字库（SD 卡）

### 4.1 文件与格式
SD 卡根 `SYSTEM/FONT/`：`UNIGBK.BIN`（双段结构）+ `GBK12/16/24/32.FON`（MSB 优先、列扫描）。

### 4.2 UNIGBK 双段结构（核心坑）
- 段 1（前 ~21792 条）：`[uni_lo,uni_hi,gbk_lo,gbk_hi]` 小端，按 Unicode 升序。
- 全 0 填充 1 条。
- 段 2（其余）：`[gbk_lo,gbk_hi,uni_lo,uni_hi]` 小端，按 GBK 升序。
- **不能整体二分**，初始化定位段 1 边界（第一条全 0）后仅在段 1 内二分。
- GBK 字段文件里小端，返回时**交换字节序**，否则汉字错位（"时/钟"→"笔/又"）。

### 4.3 LVGL 自定义字体桥（unicode→GBK→转置）
1. UTF-8 → Unicode（LVGL 解码）。
2. UNIGBK 段 1 二分得 GBK（注意小端交换）。
3. 偏移 `190*(qh-0x81)+(ql-0x40或0x41)` 取原始点阵（MSB 优先、列扫描）。
4. 转置成 LVGL 1bpp 行优先位图。
5. ASCII 用移植的 8×16 / 12×24 点阵回退（避 Montserrat 小字号发虚）。
每个字号绑定一个 `lv_font_t`，UI 直接引用。

## 五、LVGL 集成注意

- **内存池**：Zephyr LVGL 模块默认 `LV_Z_MEM_POOL_SIZE=2048`（2KB）太小，
  创建数个 label/timer 会野指针总线错误；设 `65536`（64KB）。
- **tick 来源**：Zephyr 的 `lv_conf.h` 设 `LV_TICK_CUSTOM`，时间由 `k_uptime_get_32()`
  提供；应用线程只周期调 `lv_timer_handler()`，无需（也无法）调 `lv_tick_inc()`。
- 字库文件需用户按 §4.1 放入 SD 卡；缺失时中文行回退空白，状态显示 `字库 未加载`。

## 六、Zephyr Shell 命令控制台

`CONFIG_SHELL=y` + 串口后端（dts chosen）。`app/shell_cmds.c` 用 `SHELL_CMD_REGISTER`
注册，可整文件拷到任意 Zephyr 工程复用。常用命令：
| 命令 | 说明 |
|---|---|
| `sys` | Zephyr/LVGL 版本、运行时间、主频、主栈 |
| `font` | SD 卡字库状态（UNIGBK + GBK12/16/24/32） |
| `lvmem` | LVGL 堆统计（total/free/biggest/used%/frag%） |
| `kernel uptime` / `kernel threads` | 运行时间 / 线程列表 |
| `device` | 设备树设备列表 |

提示符用 `shell_prompt_change()` 修改（需 `CONFIG_SHELL_PROMPT_CHANGE=y`）。

## 七、下载与调试（Cortex-Debug F5）

`.vscode/launch.json` + `tasks.json`：F5 先 `build`（用 `python -m west build`）再启动
OpenOCD 调试。`serverpath` 指向 `openocd.exe`，`searchDir` 指向 scripts，
`configFiles=tools/openocd.cfg`，`device=STM32H743ZI`，`rtos=Zephyr`。
`tools/read_serial.py` 一键复位目标并抓取启动日志。

## 八、真机验收标志

- 板载 LED 500ms 心跳（系统存活）。
- 串口启动日志：`*** Booting Zephyr OS build v3.7.0 ***` + `FONT: UNIGBK=1 GBK12=1 ...`
  + `SYS: Zephyr 3.7.0, LVGL 8.4.0, core clock 480000000 Hz`。
- halt 后 PC 落在 OLED 刷新路径（`mipi_dbi_spi_write_helper`），确认持续渲染无总线错误。
- 中文经 SD 字库实时取模渲染，数字/字母用移植 ASCII 点阵，未烧字表到 Flash。

## 九、工程构建与 west 实操要点（009 真机教训）

### 9.1 CMakeLists 工具链自动探测（find_package(Zephyr) 前）
删除 `build/` 后重新编译常报 `ZEPHYR_TOOLCHAIN_VARIANT not set ... Could not find Zephyr-sdk`（致命）。
在 `find_package(Zephyr)` 前加自动探测，零硬编码机器路径：
```cmake
if(NOT DEFINED ZEPHYR_TOOLCHAIN_VARIANT)
  set(ZEPHYR_TOOLCHAIN_VARIANT gnuarmemb)
endif()
if(NOT DEFINED GNUARMEMB_TOOLCHAIN_PATH)
  find_program(ARM_GCC arm-none-eabi-gcc)
  if(ARM_GCC)
    get_filename_component(_BIN "${ARM_GCC}" DIRECTORY)
    get_filename_component(GNUARMEMB_TOOLCHAIN_PATH "${_BIN}/.." ABSOLUTE)
  endif()
endif()
find_package(Zephyr REQUIRED HINTS <zephyr module path>)
```

### 9.2 删除 build/ 后必须支持全新构建
- `west build -t clean` 在 `build/` 不存在时会触发一次无 BOARD 的伪 configure（被当 WARN 吞掉）；
  `build_oneclick.bat` 的 clean 步骤改 `if exist build` 才执行，避免缺目录误报。
- `tasks.json` 的 `build` 任务必须带 `-b <board>/<soc>`：`python -m west build -b nucleo_h743zi/stm32h743xx -d build -s .`
  （缺 `-b` 删除 build 后必失败）。顶层 `options.env` 注入 `ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb`。

### 9.3 build_oneclick.bat（PowerShell/CI 友好）
- 去掉阻塞 `pause`，收尾 `exit /b %ERR%` 直接返回真实错误码（阻塞 `pause` 在 PowerShell 下会卡死或返回 255）。
- 纯 ASCII/CRLF；`cd` 先去 `%~dp0` 尾随 `\`（见 `stm32-project-scaffold` 第十节·致命坑 4）。
- 本沙箱禁用 `cmd.exe`，`.bat` 端到端仅能在用户本机验证；其内核命令即已验证通过的 `west build`。

### 9.4 PowerShell 把 west 的 stderr 误报成红色错误
`west build` 进度打到 stderr，PowerShell 包装成 `RemoteException`/`NativeCommandError`（红字），但
`BUILD_EXIT=0`、elf 正常生成。判定以**退出码**为准，别被红色吓到（详见 `stm32-ai-dev-environment` 4.8）。
