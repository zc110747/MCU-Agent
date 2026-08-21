# STM32H743 + Zephyr + LVGL OLED 中文显示项目

基于 **Zephyr RTOS** 与 **LVGL v8** 驱动 240×240 ST7789 OLED，从 **SD 卡** 实时读取
**GBK 中文点阵字库** 进行显示。工程以 `cmake + ninja` 管理，`arm-none-eabi-gcc`
（gnuarmemb）编译，`OpenOCD + ST-Link` 下载与调试，VS Code + Cortex-Debug 进行
单步调试。

> 工作区：`D:/data/workspace/stm32_zephyr`（仅在此目录内工作）。

---

## 1. 硬件资源映射

| 功能            | 外设 / 引脚                         | 说明                                            |
|-----------------|-------------------------------------|-------------------------------------------------|
| MCU             | STM32H743ZIT6                       | Cortex-M7，HSE 25 MHz 外部无源晶振              |
| OLED 显示       | ST7789V，240×240，SPI6（MIPI-DBI）  | 4 线 SPI                                        |
| OLED SCK        | PG13                                | SPI6_SCK                                        |
| OLED MOSI       | PG14                                | SPI6_MOSI                                       |
| OLED CS         | PG8                                 | SPI6_NSS（gpio cs）                             |
| OLED DC（数据/命令） | PG15                          | GPIO，高=数据                                   |
| OLED 背光 BL     | PG12                                | GPIO，高电平点亮（gpio-leds `oled_bl`）         |
| SD 卡           | SDMMC1，4-bit                       | 逻辑盘符 `1:`                                   |
| SD D0..D3       | PC8 / PC9 / PC10 / PC11             |                                                 |
| SD CLK          | PC12                                |                                                 |
| SD CMD          | PD2                                 |                                                 |
| 调试串口         | USART1（PA9 TX / PA10 RX）          | Zephyr console / shell（115200 8N1）            |
| 系统状态 LED     | 板载 User LD1（green_led）          | 心跳闪烁，验证系统存活                          |

> 板级目标选用 `nucleo_h743zi`（同款 STM32H743ZI SoC），所有引脚与自定义板一致。

---

## 2. SD 卡字库文件

需将以下文件放入 SD 卡根目录的 `SYSTEM/FONT/` 文件夹（Zephyr FatFs 逻辑盘符
为 `SD:`，对应 `CONFIG_SDMMC_VOLUME_NAME="SD"`）：

| 文件              | 作用                                              |
|-------------------|---------------------------------------------------|
| `UNIGBK.BIN`      | Unicode→GBK 映射表（4 字节/记录，**双段结构**）   |
| `GBK12.FON`       | 12×12 点阵（MSB 优先，列扫描）                    |
| `GBK16.FON`       | 16×16 点阵                                        |
| `GBK24.FON`       | 24×24 点阵                                        |
| `GBK32.FON`       | 32×32 点阵                                        |

这些是标准的 GBK 点阵字库（列扫描、MSB 优先），与正点原子 / 野火等常见
“GBK 字库” 格式兼容。

> **⚠️ `UNIGBK.BIN` 是双段结构（真机验证）**：
> - **段 1**（约前 21792 条）：`[unicode_lo, unicode_hi, gbk_lo, gbk_hi]`
>   （小端），**按 Unicode 升序**，覆盖符号区与全部汉字
>   （如 `中 U+4E2D → GBK D0D6`）；
> - 1 条全 0 填充记录；
> - **段 2**（其余）：`[gbk_lo, gbk_hi, unicode_lo, unicode_hi]`
>   （小端），**按 GBK 升序**。
>
> 两段排序键不同，**不能对整个文件做 Unicode 二分查找**。
> `lcd_driver_unigbk_lookup()` 在初始化时自动定位段 1 边界（第一条全 0
> 记录，`unigbk_find_seg1()`）并仅在段 1 内二分。

---

## 3. 软件架构 / 模块化

```
stm32_zephyr/                # 工程根目录（west 应用即根目录）
├── west.yml                 # west manifest（zephyr 3.7.0 + 模块）
├── CMakeLists.txt           # 应用构建（app + bsp，链接 LVGL）
├── prj.conf                 # Zephyr / LVGL / FatFs / 显示 配置
├── boards/
│   └── nucleo_h743zi.overlay# 设备树：SPI/MIPI-DBI/ST7789、SDMMC1、USART1、LED
├── tools/
│   ├── openocd.cfg          # ST-Link SWD 配置（adapter speed 4000）
│   └── read_serial.py       # 复位目标并抓取串口启动日志
├── app/
│   ├── main.c               # 启动：背光、挂 SD、注册字体、LVGL 泵 + 心跳
│   ├── ui.c / ui.h          # LVGL 界面（朴素深色卡片，16px 点阵字体）
│   └── (ui 通过 bsp 字体渲染中文)
├── bsp/
│   ├── drv_oled_fonts.h     # pFONT 字体描述符类型
│   ├── drv_oled_text.c/.h   # GBK 字库读取（FatFs）+ UNIGBK 二分查找
│   ├── lv_port_font.c/.h    # LVGL v8 自定义字体桥（unicode→GBK→转置）
│   ├── ascii_1608_table.c/.h# 8x16  ASCII 点阵（12/16/32px 字体的回退）
│   └── ascii_2412_table.c/.h# 12x24 ASCII 点阵（24px 字体的回退）
└── build/                   # 构建产物（west build -d build）
```

`Drivers/`（HAL）与 `third_party/`（zephyr、hal_stm32、cmsis、fatfs、lvgl）
由 Zephyr 模块体系提供；裸机驱动仅 `drv_oled_*` 与 `lv_port_font` 被保留并
移植，显示 / SD 控制器改用 Zephyr 原生驱动（`st7789v` 显示、`sdmmc_stm32`
磁盘、`FatFs`）。

---

## 4. 中文渲染原理

LVGL 把 UTF-8 文本解码成 Unicode 码点后，对每个字符调用自定义字体的
`get_glyph_dsc` / `get_glyph_bitmap`：

1. **Unicode → GBK**：用 `UNIGBK.BIN` 在段 1 内二分查找得到 2 字节 GBK 码
   （`lcd_driver_unigbk_lookup`）。⚠️ **GBK 字段在文件里也是小端
   （`[gbk_lo, gbk_hi]`）**，函数返回时需交换成 `(gbk_hi, gbk_lo)` 常规顺序，
   否则所有汉字会错位显示（实测“时/钟”会显示成“笔/又”）。
2. **GBK → 原始点阵**：按 `190*(qh-0x81) + (ql-0x40或0x41)` 计算偏移，
   从对应 `GBKxx.FON` 读取（`lcd_driver_get_hzmat_raw`）。原始数据是
   **MSB 优先、列扫描**。
3. **转置**：列扫描 → 行扫描（保持 MSB 优先），得到 LVGL 1bpp 所需的
   **行优先、MSB 优先** 位图（`glyph_transpose`，见 `lv_port_font.c`）。
4. **ASCII / 拉丁**：从裸机工程移植两套点阵字库，避免抗锯齿 Montserrat
   在小字号下混排发虚：
   - **8×16**（`bsp/ascii_1608_table.c`，95 字符 0x20-0x7E）—— 作为
     12/16/32px GBK 字体的回退；
   - **12×24**（`bsp/ascii_2412_table.c`，95 字符 0x20-0x7E）—— 作为
     24px GBK 字体的回退，保证大字号下英文/数字也清晰。
   两表都已按 LVGL 惯例位反转为 MSB 优先。

每个字号（12/16/24/32）对应一个 `lv_font_t` 实例，经
`lv_port_font_init()` 绑定 `pFONT` 与回退字体后，由 UI 直接引用
（如 `lv_obj_set_style_text_font(label, &gbk_font_24, 0)`）。

---

## 5. 构建环境

依赖：`arm-none-eabi-gcc`（gnuarmemb）、`cmake`、`ninja`、`west`、Python。
Zephyr 版本 3.7.0（见 `west.yml`，模块位于 `third_party/zephyr`）。

```bash
# 设置环境（每次新 shell 都需要）
export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH=<arm-none-eabi 安装路径>   # Windows 路径，如 D:/Software/tools/arm-none-eabi
export PATH="$GNUARMEMB_TOOLCHAIN_PATH/bin:$PATH"

# 构建（无需手动 export ZEPHYR_BASE，west 从 west.yml 自动解析；
#        本机 west 不在 PATH，用 python -m west 调用）
python -m west build -b nucleo_h743zi/stm32h743xx -d build -s .

# 产物
#   build/zephyr/zephyr.elf   (含调试信息，Cortex-Debug 使用)
#   build/zephyr/zephyr.bin
#   build/zephyr/zephyr.hex
```

> 注：`CMakeLists.txt` 通过 `Zephyr_DIR`（由 west 注入的 `ZEPHYR_BASE` 推导）
> 定位 `ZephyrConfig.cmake`。若在 MSYS2/Git Bash 下手动 export `ZEPHYR_BASE`，
> 务必使用 Windows 路径（`D:/...`）而非 POSIX 路径（`/d/...`），否则原生
> cmake 无法解析。调试文件使用相对/生成路径，不含本机绝对路径。

---

## 6. 下载与调试

### 方式一：OpenOCD 命令行烧录（已验证）
```bash
openocd -s <openocd>/share/openocd/scripts \
  -f tools/openocd.cfg \
  -c "program build/zephyr/zephyr.elf verify reset exit"
```
`openocd.cfg` 已使用 `transport select swd`（OpenOCD 0.12 新 st-link 驱动）。

### 方式二：VS Code + Cortex-Debug（F5 一键编译调试）
已提供 `.vscode/launch.json` 与 `.vscode/tasks.json`：
- **F5** 会先执行 `build` 任务再启动 OpenOCD 调试（`preLaunchTask = "build"`）。
  `build`/`clean` 使用 `python -m west build`（本机 `west` 不在系统 PATH，
  必须通过 python 模块方式调用），并注入
  `ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb`、`GNUARMEMB_TOOLCHAIN_PATH` 与
  `arm-none-eabi/bin`、`openocd/bin` 的 PATH。
- 任务：`build`（Ctrl+Shift+B 默认）/ `clean` / `flash` / `flash-openocd`；
  调试配置：`executable = build/zephyr/zephyr.elf`，
  `serverpath = D:/Software/openocd/bin/openocd.exe`，
  `searchDir = D:/Software/openocd/share/openocd/scripts`，
  `configFiles = tools/openocd.cfg`，`device = STM32H743ZI`，
  `rtos = Zephyr`。可直接下断点单步调试。
- 前置条件：安装 **Cortex-Debug** 扩展（ms-vscode?/marus25.cortex-debug）后
  按 F5 即可。首次会重新编译（增量）并自动连接 ST-Link。

串口（PA9/PA10，115200）会打印启动日志与 `FONT:` / `SYS:` 状态，
用于确认 SD 字库挂载结果与版本/时钟信息（可运行 `tools/read_serial.py`
一键复位目标并抓取启动日志）。

---

## 7. 运行表现（验收）

上电后：
- 板载 LED 以 500 ms 周期心跳闪烁 → 系统存活（UI 同步显示 `LED 心跳中`）；
- OLED 背光点亮（PG12）；
- 屏幕为**朴素深色卡片式 UI**（`ui.c`）：
  1. 纯黑背景 + 6 个圆角深色卡片（标题条/版本/主频/运行/字库/LED）
  2. 全部文字统一白色（无彩色圆点、无状态变色），信息自上而下：
     `STM32H743 中文显示`、`Zephyr 3.7.0 | LVGL 8.4.0`、
     `主频 480 MHz`、`运行 mm:ss`、`字库 已加载`、`LED 心跳中`
  3. 底部提示 `240x240 ST7789 SPI6`
- 中文经 SD 字库实时取模渲染（16×16 点阵），数字/字母用移植的 8×16
  点阵 ASCII，未烧录任何字表到 Flash。

**已真机验证**（ST-Link V2.1 + OpenOCD 0.12，SWD）：
- 固件烧录成功、校验通过、复位运行；
- 串口启动日志：`*** Booting Zephyr OS build v3.7.0 ***` +
  `FONT: UNIGBK=1 GBK12=1 GBK16=1 GBK24=1 GBK32=1` +
  `SYS: Zephyr 3.7.0, LVGL 8.4.0, core clock 480000000 Hz`；
- halt 后 PC 落在 `mipi_dbi_spi_write_helper`（OLED 刷新路径），确认程序正在
  持续渲染 OLED、无总线错误。

---

## 8. 关键实现要点 / 已知限制

- **时钟树**：目标板为 25 MHz 无源晶振（非 Nucleo 的 8 MHz ST-Link 旁路时钟），
  已在 overlay 覆盖 `&clk_hse`（25 MHz，去 `hse-bypass`）与 `&pll`（`div-m=5,
  mul-n=192`），保持 VCO 960 MHz、SYSCLK 480 MHz。
- **SDMMC 时钟**：STM32H743 的 SDMMC 时钟源为 PLL1Q，驱动在
  `CONFIG_SDMMC_STM32_CLOCK_CHECK=y` 下强制要求 48 MHz（否则 `-ENOTSUP` 导致
  `f_mount` 返回 `FR_NOT_READY`）。已设 `&pll { div-q = <20> }` → PLL1Q =
  960/20 = 48 MHz；并给 `&sdmmc1` 加 `bus-width = <4>` + `idma`。
- **SD 卷名**：Zephyr FatFs 使用字符串卷名（`FF_STR_VOLUME_ID=1`，
  `FF_VOLUME_STRS = ...,"SD",...`），逻辑盘符必须用 `SD:`（配合
  `CONFIG_SDMMC_VOLUME_NAME="SD"`），不能用数字 `1:`，否则挂载失败。
- **LVGL 内存池**：Zephyr LVGL 模块默认 `LV_Z_MEM_POOL_SIZE=2048`（2 KB）
  静态堆，不足以创建数个 label/timer，曾导致 `lv_timer_resume` 野指针总线
  错误；已在 prj.conf 设为 `65536`（64 KB）。
- **tick 来源**：LVGL 时间由 Zephyr `k_uptime_get_32()` 提供（Zephyr 的
  `lv_conf.h` 已设 `LV_TICK_CUSTOM`），应用线程只需周期性调用
  `lv_timer_handler()`，无需（也无法）调用 `lv_tick_inc()`。
- **OpenOCD 传输**：OpenOCD 0.12 新版 st-link 驱动用 `dapdirect`，须用
  `transport select swd`（旧 `hla_swd` 会报 "adapter doesn't support"）。
- **ST7789 显示开启（DISPON）**：Zephyr 的 st7789v 驱动初始化序列**不会发送
  `DISPON`(0x29)**，面板保持 sleep-in 全黑。应用必须调用
  `display_blanking_off()`（见 `app/main.c` 步骤 1b）才能点亮。此为本项目
  移植自裸机（裸机 init 末尾有 `0x29`）时的关键差异。
- **ST7789 软件复位延时**：用户板未接硬件 RST 引脚，Zephyr 走
  `mipi_dbi_spi_reset → -ENOTSUP → SWRESET(0x01)` 分支；上游仅延时 5 ms，
  远小于 ST7789 复位时间（~120 ms），导致复位未完成前发送的初始化命令
  （含 SLPOUT）被面板忽略、显示停留在 sleep-in。已 patch
  `third_party/.../display_st7789v.c`：SWRESET 后 `k_sleep(K_MSEC(120))`
  （与裸机上电 10 ms 后直接配置对齐）。注意：patch 在 `west update` 后
  可能被覆盖，重装模块后需重新应用。
- 字库文件（`.FON` / `UNIGBK.BIN`）需由用户按第 2 节放入 SD 卡；缺失时
  中文行回退为空白 / 缺字（状态行显示 `字库 未加载`）。

---

## 9. 执行步骤小结

1. 移植 Zephyr（west 工作区 + 模块：`zephyr` / `hal_stm32` / `cmsis` /
   `fatfs` / `lvgl`）。
2. 实现 SD 卡读取（SDMMC1 + FatFs，逻辑盘 `1:`）。
3. 实现 OLED 显示（ST7789V 经 SPI6/MIPI-DBI，PG12 背光）。
4. 集成 LVGL：自定义 GBK 字体桥（unicode→GBK→转置），构建中文 UI。
5. 修正时钟树（25 MHz HSE → 480 MHz）与 LVGL 内存池（2 KB → 64 KB）。
6. 编译（零警告）→ OpenOCD/ST-Link 烧录 → 串口验证 → 总结写入本 README。
