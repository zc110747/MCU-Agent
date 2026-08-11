# H743-NES — STM32H743 LVGL 菜单 + NES 模拟器

基于 **STM32H743ZIT6** 的嵌入式工程：用 **LVGL v8** 实现一级菜单框架，并在其中集成一个纯 C 编写的 **NES（FC）模拟器核心**。板载无实体按键，所有 UI 操作均通过 **调试串口（USART1 / ST-Link VCP）** 或 **USB CDC 虚拟串口** 以文本命令驱动。

> 固件标识：`H743-NES v1.0.0`（由 `status` 命令与 About 页报告）。

---

## 1. 硬件平台

| 项目 | 配置 |
| --- | --- |
| 主控 | STM32H743ZIT6（Cortex-M7，480 MHz） |
| 外部晶振 | HSE **25 MHz** 无源晶振（`HSE_VALUE=25000000`） |
| 显示屏 | ST7789 **240×240** OLED，SPI6 直连 |
| SD 卡 | SDMMC1，挂载盘符 `1:` |
| 中文字库 | 卡上 `1:/SYSTEM/FONT/`（GBK 字模） |
| 调试串口 | USART1 **PA9/PA10**（ST-Link 虚拟串口，115200-8-N-1） |
| USB | OTG_FS **PA11/PA12**，TinyUSB CDC 虚拟串口 |
| LED | 心跳指示灯（主循环驱动） |
| 实体按键 | **无** —— 所有输入来自串口 |

显示引脚（SPI6，参考）：PG13 SCK / PG14 MOSI / PG8 CS / PG15 DC / PG12 BL。

### 已剔除的硬件（相对原始 TRE_Flowers 工程）
GPS、温湿度、气压、FM 收音、VS1053 音频、外扩 SRAM。模拟器核心**无 APU**（不发声），仅实现 6502 CPU + 扫描线 PPU + Mapper。

---

## 2. 软件架构

模块化目录布局，所有路径相对工程根目录，无绝对路径：

```
stm32_nes_emulator/
├── app/                 # 应用层：菜单框架、页面、命令解析、主循环
│   ├── app_main.c/h     # 启动顺序 + 主循环（application_init/run）
│   ├── app_menu.c/h     # 页面注册表 + 导航状态机
│   ├── app_page.h       # 页面描述符（4 回调：create/tick/key/handle）
│   ├── app_pages.c      # 注册顺序即菜单顺序
│   ├── app_cmd.c/h      # 行缓冲控制台解析器（本板唯一输入设备）
│   └── page_nes.c       # NES 页面：ROM 浏览器 + 全屏模拟器
├── bsp/                 # 板级支持包：驱动 + 控制台 + 虚拟按键
│   ├── bsp_console.c/h  # UART+USB CDC 统一 RX 环形缓冲，_write 双口输出
│   ├── bsp_key.c/h      # 虚拟按键：事件队列（驱动 LVGL）+ 电平掩码（驱动 NES pad）
│   ├── drv_spi_oled.c/h # SPI6 OLED 驱动（含 may_alias TXDR 访问）
│   ├── drv_oled_fonts.* # GBK 字模（ASCII 来自编译内表，CJK 需卡上文件）
│   ├── drv_rtc.*        # RTC 驱动（status/time 命令、FatFs 时间戳）
│   ├── drv_usb_cdc.*    # TinyUSB CDC 初始化
│   ├── usb_descriptors.c# USB 描述符（用 UID_BASE 生成序列号）
│   └── disk_interface.c # FatFs diskio 桩（USB 桩返回 RES_NOTRDY）
├── Drivers/             # HAL + CMSIS
├── third_party/         # 第三方：fatfs / lvgl / tinyusb / nes
│   ├── nes/             # NES 核心：6502 + PPU + Mapper(0/1/2/3/4/7)，纯 C
│   ├── lvgl/            # LVGL v8（LV_COLOR_16_SWAP=0，仅 GBK 字体可用）
│   ├── tinyusb/         # TinyUSB 0.21.0（手选 dwc2 源）
│   └── fatfs/           # FatFs
├── Core/                # 启动文件、系统初始化、中断向量
├── cmake/               # gcc-arm-none-eabi 工具链文件
├── scripts/             # 主机侧脚本（Python 串口测试、字模/配置生成）
├── STM32H743ZITX_FLASH.ld # 链接脚本（DTCM/AXI/RAM_D1/RAM_D2/FLASH 段）
├── CMakeLists.txt       # 工程定义
├── openocd.cfg          # OpenOCD 配置（stlink + swd + stm32h7x）
└── .vscode/             # launch / tasks / settings（Cortex-Debug + 串口测试）
```

### 内存布局要点
- **机器状态**（约 82 KB，含 6502/ PPU 上下文）放入 `.dtcm`（`0x20000000`，128 KB 紧耦合）。
- **ROM 镜像**（≤ 256 KB）放入 `.ram_d2`（`0x30000000`，288 KB）。
- 构建产物实测内存占用（Release）：DTCM **62.82%**、RAM_D2 **88.89%**、FLASH 12.24%。

### 菜单与全屏
- 页面以 **const 描述符 + 4 个回调**（create / tick / key / handle）注册，`register_pages()` 顺序即菜单顺序：`nes → clock → sysinfo → keytest → about`。
- NES 页面标记 `full_screen` / `wants_display`：进入后主循环**暂停 LVGL**，直接刷 SPI6；退出后恢复 LVGL 节拍，避免两套刷新打架。

---

## 3. 构建（CMake + Ninja + arm-none-eabi-gcc）

### 工具链
- **Arm GNU Toolchain**（`arm-none-eabi-gcc` 15.3.x 或兼容版本）
- **CMake ≥ 4.2** 与 **Ninja**
- **OpenOCD**（烧录/调试）

### 配置与编译
工具链文件已通过 `CMAKE_TOOLCHAIN_FILE` 在 `project()` 前注入，无需额外参数。

```bash
# Debug 构建（默认，-Og，便于单步调试）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Release 构建（-O2，实机游戏请务必用此配置，NES 才能跑满 ~60fps）
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

产物：
- `build/nes_h743.elf`（或 `build-release/nes_h743.elf`）
- 同时生成 `.hex` 与 `.bin`（objcopy 后处理）

> **注意**：NES 核心是解释型 6502 + 扫描线 PPU，在 `-Og`（Debug）下无法达到 60 fps。**实机游玩请用 Release 配置**。

### 关键编译定义（`CMakeLists.txt`）
```
STM32H743xx  USE_HAL_DRIVER  HSE_VALUE=25000000
CFG_TUSB_MCU=OPT_MCU_STM32H7  CFG_TUSB_OS=OPT_OS_NONE
```
- 链接使用 `-specs=nano.specs`（不含 `nosys.specs`，`printf` 经强 `_write` 走串口双口）。
- 工程源码 + NES 核心套 `-Wall -Wextra`，**零错误零告警**通过。
- FatFs 单独列出，不在严格告警集中（规避 `diskio.c` 的 `res` 未初始化告警）。

### 字符编码
全工程源码统一为 **UTF-8**，编译器加 `-fexec-charset=UTF-8`。中文（GBK）字模与菜单文案以 UTF-8 写入，避免 GBK/`may_alias` 严格别名告警。

---

## 4. 烧录与调试（OpenOCD + ST-Link + Cortex-Debug）

### 命令行烧录
```bash
openocd -f openocd.cfg -c "program build/nes_h743.elf verify reset exit"
```

### VS Code
- `.vscode/launch.json`：
  - **Debug** → `build/nes_h743.elf`（Cortex-Debug，ST-Link SWD）
  - **Debug Release build** → `build-release/nes_h743.elf`
  - **Attach (no reflash)** → 仅挂载调试，不重烧
- `.vscode/tasks.json` 任务：
  - `CMake: Configure / Build / Build Release`
  - `Flash (OpenOCD)`（烧 `build/nes_h743.elf`）
  - `Flash Release (OpenOCD)`（烧 `build-release/nes_h743.elf`）
  - `Serial: self-test` / `Serial: interactive`（调用 `scripts/serial_test.py`）

---

## 5. 串口 / USB 控制台命令

两块串口（USART1 VCP 与 USB CDC）共用同一个解析器，命令完全一致。

**协议**：每行以 CR 或 LF 结束（≤ 96 字节，超长报 `ERR line too long`）；每条命令**回一行**以 `OK ` 或 `ERR ` 开头（便于脚本匹配）。默认开启回显，测试脚本用 `echo off` 关闭。

### 命令清单

| 命令 | 说明 |
| --- | --- |
| `help` / `?` | 列出全部命令 + 页面表 |
| `status` | 固件名/版本、sysclk、uptime、时钟源、当前页面、按键掩码、NES 状态与 fps |
| `pages` | 列出已注册页面（`*` 标记当前页） |
| `menu` / `back` | 关闭当前页面，返回菜单 |
| `open <page\|index>` | 按命令名或数字索引打开页面 |
| `sel <index>` | 移动菜单高亮项 |
| `key <name> [ms]` | 点按一下按键（默认点按时长 `KEY_TAP_DEFAULT_MS`，可指定 1–5000 ms） |
| `down <name>` | 按住不放 |
| `up <name>` | 释放 |
| `release` | 释放所有按键 |
| `keys` | 列出所有按键名及当前电平（DOWN / -） |
| `rom list` | 列出卡上 `*.nes` 文件（含目录路径） |
| `rom load <index>` | 加载并启动指定 ROM（加载在页面 tick 中异步进行，不阻塞控制台） |
| `rom stop` | 退出模拟器回到菜单 |
| `rom info` | 模拟器状态：mapper 号/名、帧数、fps、pad 掩码 |
| `time` | 读取 RTC |
| `time <YYYY-MM-DD> <hh:mm:ss>` | 设置 RTC |
| `echo on\|off` | 开关本地回显 |
| `reset` | 软件复位（NVIC_SystemReset） |

### 虚拟按键名称
```
up down left right          # 方向
a b select start            # A/B/选择/开始
ok back menu                # 菜单导航（别名 u 等见 bsp_key.c）
```
示例：`key start`、`down a`（按住 A）、`up a`、`release`。

### 紧急处理
- 若 `down` 导致按键卡住，发送 `Ctrl-C`（0x03）可放弃当前行并释放全部按键（`ok abort`）。
- 或在脚本结束时 `Console.close()` 自动发送 `release`。

---

## 6. NES ROM 放置

模拟器按以下顺序扫描 ROM：
1. 主目录 **`1:/NES`**
2. 回退目录 **`1:`**（卡根）

将 `.nes` 文件拷贝到 SD 卡的 `NES/` 目录，重启或执行 `rom list` 即可看到。支持 **iNES** 格式，单文件 ≤ 256 KB（ROM 缓冲区占满 RAM_D2）。

**支持的 Mapper**（由 `nes_mapper_name()` 报告）：

| 号 | 名称 | 号 | 名称 |
| --- | --- | --- | --- |
| 0 | NROM | 4 | MMC3 |
| 1 | MMC1 | 7 | AxROM |
| 2 | UNROM | | |
| 3 | CNROM | | |

> 不支持的 mapper 会在加载时被拒绝并打印原因（`not an iNES image` / `too large` / `unsupported mapper`）。

### 显示适配
NES 画面 256×240，面板 240×240：左右各裁掉 **8 列**（取中间 240 列），上下以静态带状缓冲（每 `NES_BAND_LINES=30` 行刷新一次）搬运到 ST7789 显存。

---

## 7. Python 串口自测（`scripts/serial_test.py`）

脚本依赖 `pyserial`：`python -m pip install pyserial`。

```bash
# 列出本机串口（自动识别 ST-Link VCP，默认回退到首个串口）
python scripts/serial_test.py --ports

# 完整自测：basics / navigation / keys / nes / error-handling
python scripts/serial_test.py --port COM19

# 只列出卡上 ROM
python scripts/serial_test.py --port COM19 --list

# 加载并运行第 0 号 ROM 若干秒（默认 5 s）
python scripts/serial_test.py --port COM19 --play 0 --seconds 10

# 手动交互
python scripts/serial_test.py --port COM19 --interactive
```

测试覆盖（对齐验收要求“可使用 python 访问串口测试”）：

| 测试组 | 内容 |
| --- | --- |
| `test_basics` | echo 关闭、status 横幅、pages 列表 |
| `test_navigation` | 遍历所有注册页面：open → 停留 → back |
| `test_keys` | sel 菜单导航、key 点按、down/up 持键、release 后清理 |
| `test_nes` | rom list、rom load、rom info、游玩若干秒（含 start/A）、fps≥20 判定、rom stop 回收 |
| `test_errors` | 非法命令/按键/页面/ROM 索引均返回 `ERR` 且控制台不卡死 |

脚本判定规则：每条命令读至 `OK`/`ERR` 行；`test_nes` 中 fps 低于 20 会提示“是否用了 Release 构建”。

> 若卡上无 `.nes` 文件，`test_nes` 会打印提示并跳过（不影响其余用例 PASS）。

---

## 8. 已知限制与备注

- **无实机验证**：本工程在构建沙箱中**无真实 ST-Link 硬件**，仅保证源码、配置、构建（零告警）、Python 脚本可运行与端口探测正确；实机烧录、SPI 显示、SD 读取与 NES 帧率需在真实硬件上验证。
- **无音频**：NES 核心不含 APU，不输出声音。
- **Release 优先**：Debug 构建仅供调试，游戏帧率不达标，请用 Release。
- **字体**：LVGL 仅启用 `lv_font_gbk_16`（所有 Montserrat 字体已禁用）；CJK 字形依赖卡上 GBK 字模，ASCII 字形来自编译内表（`show_fault` 页可纯 ASCII 工作）。

---

## 9. 快速上手清单

```bash
# 1) 编译 Release（推荐游玩）
cmake -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release

# 2) 烧录
openocd -f openocd.cfg -c "program build-release/nes_h743.elf verify reset exit"

# 3) 放 ROM：SD 卡 NES/ 目录放 *.nes

# 4) 串口自测（ST-Link VCP，通常 COM19）
python scripts/serial_test.py --port COM19

# 5) 手动玩
python scripts/serial_test.py --port COM19 --interactive
#   board> open nes
#   board> rom list
#   board> rom load 0
#   board> key start
#   board> key a
```
