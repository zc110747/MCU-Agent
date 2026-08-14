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
│   ├── app_pages.c      # 页面描述符实现（getter），注册顺序见 app_main.c
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
│   ├── nes/             # NES 核心：6502 + PPU + Mapper(0/1/2/3/4/7/23)，纯 C
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
- **DTCM / RAM_D2 由运行时内存池动态管理，链接期零占用**。这两个 SRAM 区不再通过链接脚本放置静态变量，而是在 `bsp/sram_pool.c` 中以边界标记（boundary-tag）空闲链表分配器统一管理，供按需分配/释放。
- **NES 模拟器仅在打开页面时占用这两块内存**：`nes_open()` 从 DTCM 分配约 82 KB 机器态（`nes_t`，含 6502/PPU 上下文）、从 RAM_D2 分配 ≤ 286 KB ROM 镜像（缓冲区已撑满 RAM_D2，原先钉死的 256 KiB 上限会拒掉 256 KiB 级卡带如魂斗罗，因其 `.nes` 文件为 256 KiB 数据 + 16 B iNES 头 = 262160 B）；`nes_close()` 在退出页面时释放归还内存池。新块与前后空闲块合并（coalescing），开/关循环不产生碎片。
- **退出后内存立即归还**，可被其它应用（如后续相机）复用，因此这两个区平时显示为空（链接期 DTCM=0B、RAM_D2=0B）。
- 构建产物实测内存占用（Release）：RAM_D1 **71.29%**、FLASH **17.40%**（详见 [第 12 节 资源占用](#12-资源占用)）。DTCM / RAM_D2 仅在 NES 运行期被占用，可通过 `status` 命令实时查看 `sram dtcm:` / `sram d2:` 空闲量。

### 菜单与全屏
- 页面以 **const 描述符 + 4 个回调**（create / tick / key / handle）注册，`register_pages()` 顺序即菜单顺序：`clock → txt → image → nes → keytest → sysinfo → about`。
- NES 页面标记 `full_screen` / `wants_display`：进入后主循环**暂停 LVGL**，直接刷 SPI6；退出后恢复 LVGL 节拍，避免两套刷新打架。
- **开机默认进入时钟页（钟表界面）**：`application_init` 在 `app_menu_init()` 后调用 `app_menu_open_cmd("clock")` 直接打开时钟页；时钟页 `on_key = NULL`，按 `BACK` / `B` / `MENU` 经默认处理返回主菜单，其余功能不变。

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
| `txt list` | 列出卡上 `*.txt` 文件 |
| `txt open <index>` | 打开并阅读指定文本（按 240×240 屏幕分页） |
| `txt close` | 离开阅读器，回到文件列表（**高亮停留在刚读的文件**，不跳回第一条） |
| `txt info` | 阅读器状态：文件名 / 编码 / 字节数 / 当前页 |
| `txt sel` | 调试：当前文件列表高亮项（索引 + 文件名） |
| `txt seed [SUB/]NAME.TXT` | 调试：写多页样例到卡（可带子目录，如 `TESTDIR/SAMPLE.TXT`） |
| `txt cddir <sub>` | 调试：把文件列表直接跳进某子目录 |
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

将 `.nes` 文件拷贝到 SD 卡的 `NES/` 目录，重启或执行 `rom list` 即可看到。支持 **iNES** 格式，单文件 ≤ **286 KB**（运行时从 RAM_D2 动态分配 ROM 镜像缓冲区，撑满整块 RAM_D2；退出 NES 页面即释放，不长期占用）。注意：286 KB 只是缓冲区上限，文件能否真正运行还取决于其 **Mapper** 是否被实现（见下表）。

**支持的 Mapper**（由 `nes_mapper_name()` 报告）：

| 号 | 名称 | 号 | 名称 |
| --- | --- | --- | --- |
| 0 | NROM | 4 | MMC3 |
| 1 | MMC1 | 7 | AxROM |
| 2 | UNROM | 23 | VRC2/VRC4 |
| 3 | CNROM | | |

> 不支持的 mapper 会在加载时被拒绝并打印原因（`not an iNES image` / `too large` / `unsupported mapper`）。
>
> 部分名为「魂斗罗 / Contra」的 `.nes` 其实是 **mapper 23（VRC2/VRC4）** 镜像（128 KB PRG + 128 KB CHR），并非标准的 UNROM(mapper 2) 魂斗罗。当前核心已**实现 mapper 23**（按 VRC4 仿真，覆盖 VRC2b/VRC4f 的 stride-1 寻址）；实机验证 Contra(J, VRC2b) 可加载并以 ~41fps 正常运行。注意 iNES 1.0 不记录 VRC 子变体，极少数 VRC4e(stride-4) 卡带（如 Boku Dracula-kun / Tiny Toon J）可能需翻转 stride，暂未覆盖。

### 显示适配
NES 画面 256×240，面板 240×240：左右各裁掉 **8 列**（取中间 240 列），上下以静态带状缓冲（每 `NES_BAND_LINES=30` 行刷新一次）搬运到 ST7789 显存。

---

## 6.1 文本阅读器（TXT）

菜单卡片「文本阅读器」读取 SD 卡上的 `.txt` 文件，**不整屏直开**，而是按 240×240 屏幕大小**分页**显示，靠方向键翻页阅读：

- 进入后先列出卡上所有 `*.txt`（目录仍可进入，子目录里的 .txt 也能找到）；`A / OK / START` 打开，`SELECT` 退回主菜单。
- 阅读界面每页约 8 行（按面板宽度自动折行），`↑` 上一页、`↓ / A / OK / START` 下一页，`B / SELECT / BACK` 返回文件列表。
- 文件读入 RAM 后分页（上限 32 KB，超出部分在页脚提示「仅显示前 32KB」）。
- 编码：默认按 **GBK** 转 UTF-8 显示（与本工程卡上文件名一致）；若文件带 **UTF-8 BOM（EF BB BF）** 则直接按 UTF-8 显示，避免乱码。
- 串口命令：`txt list` / `txt open <index>` / `txt close` / `txt info`（与 `rom`/`img` 同构，加载在页面 tick 中异步完成）。

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

## 8. UI 设计规格（白底蓝字主题）

主菜单与各页面的 UI 采用统一视觉语言。以下为设计需求与落地要点，便于后续维护与二次开发。

### 8.1 总体视觉
- **白底**（`COL_BG = 0xFFFFFF`）背景。
- **主文字蓝色**（`COL_TEXT = 0x0319`）。
- **描述 / 提示 / 状态栏浅蓝**（`COL_LABEL` / `COL_DIM = 0x64BB`）。
- 分隔线淡蓝（`COL_SEP = 0xB6FF`），选中指示浅蓝发光（`COL_SEL = 0x8DFF`）。
- 全部颜色宏集中在 `app/app_page.h`，换主题只改一处。

### 8.2 主菜单（一级，横向滑动卡片）
- 每张卡片宽 240（整屏），`LV_FLEX_FLOW_ROW` 横向排布；`← / →` 通过 `lv_obj_scroll_to_view` 切换，底栏圆点指示当前页。
- 卡片中央为**圆形图标**：74×74 圆环（未选中浅蓝、选中蓝色 3px）+ 外圈 90×90 淡蓝发光环；**无外边框矩形**。
- 卡片**仅显示 APP 名称**（去掉版本号 / 串口指令等小字 hint）：24 px（`lv_font_gbk_24`），蓝色居中，位于圆形图标正下方（卡片内相对 `y = CARD_H/2 + 22`）。
- 选中态靠圆环 + 发光环 + 蓝名区分；`A / OK / START` 进入页面，进入逻辑与旧版一致。

### 8.3 顶栏（所有页面共用 `ui_header`）
- 白底蓝字，**居中标题**（如 “APPS” “系统信息”）。
- **左上角实时时钟**：`HH:MM` 蓝色（`lv_font_gbk_16`），取自芯片 RTC（`drv_rtc_get`）；RTC 不可用显示 `--:--`。
- 刷新节奏：主循环每 pass 调 `app_menu_tick`，仅当 `HH:MM` 变化（`strcmp` 缓存比较）才 `lv_label_set_text`，避免每 5 ms 读 RTC。
- 菜单顶栏与页面顶栏各持独立句柄（`s_hdr_clock_menu` / `s_hdr_clock_page`）；页面重开时自动指向最新标签，旧对象随 `lv_obj_clean` 销毁，无野指针。

### 8.4 SD 浏览器（目录菜单）
- 目录名白色，左侧带 **文件夹图标**（16×16 RGB565），不再带 `/` 前缀。
- 子目录内按 `SELECT` 返回上一级（`browser_up`）；根目录按 `SELECT` 退出应用；状态栏文字随层级变化（根目录 “SELECT 退出” / 子目录 “SELECT 上级”）。

### 8.5 字体与图标资源
- `bsp/lv_font_gbk.*` 提供 **12 / 16 / 24 / 32 px** 四档 GBK 字体；ASCII 字形来自编译内表，CJK 字形依赖卡上 `GBKxx.FON`。
- 主菜单图标由 `tools/gen_menu_icons.py` 生成 `app/menu_icons.c/h`（48×48 白底蓝图：clock / sysinfo / keytest / nes / image / about + 16×16 folder），已加入 CMake 源列表。
- 本版 LVGL 的 `lv_img_header_t` 无 `stride` 字段（stride 由 cf 推导），生成器已剔除该字段。

### 8.6 设计演进与取舍（关键点）
- **FreeRTOS 方案已放弃**：RTOS 端口独占 SysTick 导致 HAL 时基冻结、`MX_SDMMC1_SD_Init` 卡死；用户决定回退裸机协作循环，专注 UI 优化。代码已回退干净（`startup_stm32h743xx.s` 向量表恢复 HAL 处理、删除 `FreeRTOSConfig.h` / `hal_timebase_tim.c`）。
- 白底主题前为黑底；灰色小字在白底下不可见，故描述 / 提示文字统一改为浅蓝。
- 圆形图标风格（去外边框）替代原矩形卡片，参考真机截图确定。
- **真机验证状态（2026-08-11）**：已用 OpenOCD + ST-Link V2 实机烧录，串口自测 **38/38 PASS**，UI 观感由用户在真机确认。

---

## 9. 已知限制与备注

- **实机验证**：2026-08-11 起已用 OpenOCD + ST-Link V2 在真实硬件上烧录并自测（串口自测 38/38 PASS），UI 观感由用户在真机确认；更早的“无实机验证”阶段仅覆盖源码、配置、构建（零告警）与脚本端口探测。
- **无音频**：NES 核心不含 APU，不输出声音。
- **Release 优先**：Debug 构建仅供调试，游戏帧率不达标，请用 Release。
- **字体**：LVGL 启用 `lv_font_gbk_12 / 16 / 24 / 32`（所有 Montserrat 字体已禁用）；CJK 字形依赖卡上 `GBKxx.FON`，ASCII 字形来自编译内表（`show_fault` 页可纯 ASCII 工作）。

---

## 10. 快速上手清单

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

---

## 11. 关键实现约束与踩坑（务必保留）

以下内容是工程稳定运行的关键约束与已修复的坑，改动前请先读本节。

### 11.1 中文文件名 / ROM 名渲染链路（最关键约束）
- **FatFs `FF_CODE_PAGE` 必须为 936（GBK）**，定义在 `third_party/FatFs/ffconf.h`。**切勿改回 437**：437 会把中文长文件名（LFN）转 CP437 丢字符，导致 `f_open` 命中失败、串口与屏幕全乱码。
- 渲染链路（两层 UTF-8 → GBK 转换）：
  1. 卡上文件名：`fno.fname` 经 FatFs 返回 **GBK 字节**（保留中文、保证 `f_open` 命中）。
  2. 串口输出：`bsp/gbk_conv.c` 的 `gbk_to_utf8()` 把 GBK 转 UTF-8 后打印（中文 ROM 名现正确显示）。
  3. OLED 显示：`lv_font_gbk_16/24/32` 字体驱动做 **UTF-8 → Unicode → GBK 字形** 映射，故页面必须把 GBK 名先 `gbk_to_utf8()` 转 UTF-8 再喂 `lv_label_set_text`；直接喂 GBK 字节会乱码（原 bug #1）。
- `bsp/gbk_unicode_tbl.c` 由 `tools/gen_gbk_table.py` 离线生成（23940 项 / 21791 映射，约 48 KB Flash）。
- 工程源码统一 **UTF-8 + `-fexec-charset=UTF-8`**，字符串字面量保持 UTF-8（LVGL 期望）。

### 11.2 SELECT 键退出语义
- NES 运行中：`SELECT` / `BACK` / `MENU` 任一按一下 `stop_game()` → 回 ROM 浏览器。
- ROM 浏览器中：`SELECT` → `app_menu_back()` → 回主菜单。
- `feed_pad()` 已移除 `SELECT → NES_PAD_SELECT` 映射（设计取舍：用 SELECT 换“退出”，游戏中 SELECT 按钮不再可用）。
- 入口提示：`↑↓ 选择  A 运行  SELECT 退`。

### 11.3 SD 浏览器目录导航
- 目录与文件一并列出，目录名白色 + 左侧文件夹图标，不再带 `/` 前缀。
- 非根目录列表首项插入 `.. 上级目录`；`A / OK / START` 命中目录进入子目录（`browser_enter`），命中 `..` 返回上层（`browser_up`）；命中文件才触发 `on_select`。
- 排序：先 `..`，再目录，后文件。
- 路径缓冲 `b->path` 16 → 64；`browser_enter` 临时缓冲 128 防 `snprintf` 截断告警。

### 11.4 构建与验证基线
- 编译最多 `ninja -j4`（用户要求，避免 CPU 风扇转速过高）。
- 实机烧录：`openocd -f openocd.cfg -c "init" -c "program build/nes_h743.elf verify reset exit"`（ST-Link V2；COM19 = USART1 VCP，COM4 = USB CDC，共用 `app_cmd` 解析器）。
- 串口自测 **38/38 PASS**（`scripts/serial_test.py`：NES 31 fps、mapper 0、中文名正确）；`scripts/verify_features.py` 专项验证中文名 + SELECT 退出 **3/3 PASS**。

---

## 12. 资源占用（2026-08-14 Release 实测）

以下数据取自 `build-release/nes_h743.map`（链接器汇总）与 `arm-none-eabi-size` 模块分解，固件 `H743-NES v1.0.0`，菜单 7 页注册（clock / txt / image / nes / keytest / sysinfo / about）。

### 12.1 链接器内存区域汇总

**Release（-O2，实机运行版本）：**

| 区域 | 占用 | 容量 | 占比 | 说明 |
| --- | --- | --- | --- | --- |
| FLASH | 364 892 B | 2 MB | **17.40%** | `.text` 209 092 + `.rodata` 154 660 + `.data` 448 + 向量表/其它 |
| RAM_D1 (AXI-SRAM) | 373 752 B | 512 KB | **71.29%** | `.data` + `.bss` + LVGL 堆 + 栈 + SD 扇区缓冲，栈堆在此 |
| RAM_D2 | 0 B | 288 KB | 0.00% | 链接期零占用；NES 运行期动态分配 ≤286 KB ROM 镜像 |
| DTCM | 0 B | 128 KB | 0.00% | 链接期零占用；NES 运行期动态分配 ~82 KB 机器态 |
| RAM_D3 | 96 B | 64 KB | 0.15% | USB DMA 缓冲（non-cacheable） |

**Debug（-Og，仅供调试）：** FLASH 360 564 B（17.19%）、RAM_D1 365 KB（71.29%）；DTCM / RAM_D2 同样为零。Debug 帧率不达标，实机游玩请用 Release。

`text/data/bss` 汇总：Release `364424 / 456 / 373400`，Debug `360100 / 456 / 373408`。

### 12.2 模块分解（Release，链接后实际占用）

按 map 文件最终输出区统计（含各段 gc-sections 后的真实贡献）：

| 模块 | FLASH (B) | FLASH 占比 | RAM (B) | 说明 |
| --- | ---: | ---: | ---: | --- |
| LVGL v8 | 110 856 | 30.4% | 41 982 | 控件库 + 显示驱动 + 内部堆 |
| 字库/GBK | 107 856 | 29.6% | 8 293 | `lv_font_gbk` + GBK↔Unicode 映射表（~48 KB rodata）+ 字模缓存 |
| 应用层 app | 52 609 | 14.4% | 254 300 | 菜单/页面/命令解析；RAM 大头：`img_decode` 图像解码缓冲 128 KB + `page_txt` 文本缓冲 ~104 KB |
| HAL 驱动 | 22 121 | 6.1% | 9 | STM32H7xx HAL |
| 其它/系统 | 14 883 | 4.1% | 447 | libc/nano、运行时 |
| LCD/OLED 驱动 | 13 407 | 3.7% | 32 308 | SPI6 + ST7789 + 文本渲染 |
| USB CDC | 13 213 | 3.6% | 2 275 | TinyUSB + 描述符 |
| NES 核心 | 12 920 | 3.5% | 16 009 | 6502 + PPU + Mapper(0/1/2/3/4/7/23)，运行期再动态分配 DTCM/RAM_D2 |
| FatFs | 10 075 | 2.8% | 522 | 含 CP936 中文字符表（体积增大来源） |
| BSP 其它 | 3 277 | 0.9% | 684 | 控制台、虚拟按键、SRAM 池 |
| CMSIS/启动 | 1 816 | 0.5% | 422 | 启动文件 + 系统初始化 |
| SD 驱动 | 1 056 | 0.3% | 1 | SDMMC1 |
| **合计** | **364 089** | 100% | 357 252 | 与链接器汇总差 ~800 B（fill/对齐） |

> 注：RAM 合计 357 252 B 为 `.data` + `.bss`；链接器报 RAM_D1 373 752 B 的差额 ~16.5 KB 为链接脚本预留的栈（Stack）+ LVGL 堆（`LV_MEM_SIZE`）等区域。

### 12.3 关键资源观察

- **FLASH 余量约 1.7 MB**（82.6%），后续新增功能（如相机应用）的主要瓶颈在 RAM 而非 Flash。
- **RAM_D1 已用 71%**，剩余 ~138 KB；`.bss` 大头来自应用层静态缓冲（`img_decode` 图像解码 128 KB、`page_txt` 文本 ~104 KB、NES 页 15.7 KB）、LVGL 堆 40 KB 与显示缓冲 28.8 KB。若需扩容可调整静态缓冲策略（如图像解码缓冲改走 RAM_D2 动态池，`img_decode`/`page_txt` 均为链接期静态占用，是后续优化点）。
- **DTCM + RAM_D2 链接期 0 占用**，仅在 NES 打开时动态占用（~82 KB + ≤286 KB），退出即归还 —— 这是为后续相机等应用预留的空间。
- NES 运行期（ROM 加载后）RAM_D2 空闲低至 ~2 KB（魂斗罗 286 KiB 镜像）；`status` 命令实时可见 `sram dtcm:` / `sram d2:`。
