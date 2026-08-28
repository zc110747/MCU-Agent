# STM32H743 OLED 信息面板 (emWin 版)

基于 **STM32H743ZIT6 + ST7789 240×240 OLED + STemWin** 移植自裸机参考工程
`003.stm32h743_lvgl_oled`（LVGL 版），在保持相同硬件、相同界面与相同功能
的前提下，用 **emWin (STemWin v5.44)** 重新实现整套显示栈。

> 目标：界面/功能与 003 的 LVGL 版 1:1 对齐；工程可 **Debug + Release 零警告**
> 通过 CMake + Ninja + arm-none-eabi-gcc 构建。

---

## 1. 硬件

| 项目 | 规格 |
|------|------|
| MCU  | STM32H743ZIT6 (Cortex-M7, 480 MHz, HSE 25 MHz) |
| 屏幕 | ST7789 240×240 RGB565, SPI6 半双工 TX-only |
| SPI6 | SCK=PG13, MOSI=PG14, NSS=PG8(HW), DC=PG15, BL=PG12 |
| 存储 | SD 卡 (SDMMC1 4-bit), FatFs 挂载 `1:/SYSTEM/FONT/` |
| 字库 | `UNIGBK.BIN` + `GBK12/16/24/32.FON` (SD 卡中文点阵) |
| 调试 | USART1 (PA9/PA10) 115200 8N1, OpenOCD + ST-Link, SWD |

---

## 2. 界面与功能

与 003 LVGL 版一致的深色信息面板：

- 顶部标题栏：`STM32H743 信息面板`
- 32px 实时时钟 (时:分:秒) + 16px 日期 + 星期
- SD 卡容量：文件系统名 / 已用 / 总量 / 百分比 + 进度条
- 板载信息：主频 (480 MHz / HSI 备用)、运行时间、字库状态、缓存命中率
- **字库缺失故障页**：红底 `SD / FONT ERROR`，列出 5 个必需文件路径

显示管线：`GUI_DispString*` / `GUI_FillRect` / `GUI_DrawPixel`
→ 本地 VRAM `gui_vram[240*240]` (RGB565) → `OLED_CopyBuffer()` 刷写到 ST7789。

---

## 3. 构建要求（重要：STemWin 与 binutils 2.44 不兼容）

STemWin 官方预编译库 `STemWin_CM7_wc16.a` 由 **ARM Compiler (armcc)** 构建。
其目标文件是合法的 Arm/Thumb 代码，但 **GNU ld 2.44（随 arm-none-eabi-gcc 15.x
发布）在解析函数间 interworking 重定位时会报错并中止链接**：

```
(.text.xxx+0x..): undefined reference to `GUI_xxx'
(GUI_xxx): Unknown destination type (ARM/Thumb)
dangerous relocation: unsupported relocation
```

**已验证的根因**：库目标文件缺 `.type %function` / 映射符号相关的严格性检查，
binutils 2.44 收紧后直接拒绝。社区公认修复方案是 **使用 binutils < 2.44 的工具链**
（GNU Arm Embedded **13.3.rel1 / 14.2.rel1**）。

### 本工程的处理方式

工具链通过 **绝对路径强制锁定**，杜绝任何 PATH 回退：

- `cmake/gcc-arm-none-eabi.cmake` 探测 `arm-gnu-toolchain-14.2.rel1`
  （或环境变量 `ARM_GNU_TOOLCHAIN`）后，将编译器/链接器/ar 等**全部以绝对路径
  `FORCE` 写入 CMake 缓存**，并追加 `-B<tc_bin>` 让 gcc 驱动优先在 14.2.rel1 的
  `bin` 中解析 `ld`/`as`，即使 PATH 上另有 15.3.1 也不会被误用。
- `build_oneclick.bat` 显式传入 `-DCMAKE_TOOLCHAIN_FILE=...`，并同时构建
  **Debug 与 Release** 两个配置。
- 已验证可用的工具链：`GNU Arm Embedded 14.2.rel1`（gcc 14.2.1 / binutils 2.43.1）。

### 链接阶段修复的两类问题

1. **STemWin `.a` 未被链接**：不能把预编译库直接塞进 `add_executable` 的源列表
   （CMake 会静默丢弃 `.a`），改为 `add_library(stemwin STATIC IMPORTED)` +
   `target_link_libraries(... -Wl,--start-group stemwin -Wl,--end-group)`，以处理
   STemWin 各成员间的循环引用。
2. **`LCD_*` 符号多重定义**：原 OLED 驱动（从 003 LVGL 工程带入）自有一套
   `LCD_*` 高层 API，与 emWin 库内部的 `LCD_SetColor` 等符号撞名。已将其统一
   重命名为 `OLED_*`（保留 emWin 的 `LCD_X_*` 回调与 static 的 `LCD_SPI_*`），
   冲突消除。

> 注：早期尝试过为 `.a` 补 `$t` 映射符号（`scripts/patch_stemwin.py`），但 binutils
> 2.44 的严格性仅靠映射符号无法绕过，**该脚本已废弃删除**，最终方案是降级工具链。

---

## 4. 构建

### 一键构建 (Windows)

```bat
build_oneclick.bat        :: configure -> clean -> build (Debug AND Release)
```

> 构建脚本曾报 `此时不应有 into。`，根因是 **致命坑 3 + 致命坑 4**（参考 stm32-project-scaffold skill）：
> 1. 块内 `echo` 文本里的 `(` 被 cmd 块解析器当成命令组起点——如 `(FatFs and STemWin)`、`(Debug)`、
>    `(Release)` 位于 `if (...) ( ... )` 块内的 echo 行，`)` 提前闭合块，残留 `into ...` / `Debug`
>    变成非法 token → `此时不应有 into。`。已清除所有块内 echo 的 `( )`（如 `Debug` 取代 `(Debug)`）。
> 2. `cd /d "%~dp0"` 的 `%~dp0` 永远带尾随 `\`，`\"` 被当成转义引号导致引号未闭合；已改为先剥离尾随
>    `\` 再 `cd`。`for` 块内 `2>&1` 的 `&` 在块解析期被当命令分隔符，已改为 `> nul 2>nul`。
> 已用 PowerShell 实测 `.\build_oneclick.bat` 跑通 Debug + Release 双构，退出码 0、零警告。

### 手动构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
# Release
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### VSCode

- `Tasks: Run Task -> build`（默认 Debug）
- `Run -> Debug (OpenOCD + ST-Link)` 或 `flash` 任务烧录
- IntelliSense 通过 `build/compile_commands.json` 自动解析 include / define

---

## 5. 架构要点

| 层 | 文件 | 说明 |
|----|------|------|
| LCD 驱动 | `Bsp/emWin/LCDConf.c` | `GUIDRV_LIN_16` + `GUICC_565` + 本地 VRAM，刷写走 `OLED_CopyBuffer` |
| 系统层 | `Bsp/emWin/GUIConf.c` / `GUI_X.c` | No-OS 内存池 (128 KB) / 系统 tick |
| 中文字体 | `Bsp/emWin/emwin_font_gbk.c(.h)` | 自定义 `GUI_FONT`：UTF-8 → ASCII(编译表) / 中文(lv_gbk_map → SD 点阵)，24 槽轮转缓存 |
| 应用 | `Application/app_main.c` / `app_ui.c` | 启动先初始化面板，再 `GUI_Init()`；1s 周期刷新 |
| 字库桥 | `Bsp/lv_gbk_map.c` | Unicode → GBK 映射 |

### 颜色编码注意

`GUI_ConfDefaults.h` 中 `GUI_USE_ARGB` 默认 **0**，此时 `GUI_COLOR` 为
`0x00BBGGRR`（蓝在高字节）。而 003 LVGL 的字面量是 `0x00RRGGBB`。因此所有传给
`GUI_SetColor()` 的颜色都经 `ui_col()` 做 R/B 交换，保证 ST7789 像素一致。

---

## 6. 验收状态

- [x] 零警告构建 (Debug)
- [x] 零警告构建 (Release)
- [x] FLASH / RAM 占用统计 (见下方)
- [x] 真机烧录 (OpenOCD + ST-Link, SWD) 并验证启动越过 `GUI_Init` 进入主循环

| 构建 | FLASH | RAM |
|------|-------|-----|
| Debug   | 133532 B / 2 MB (6.37%) | 271808 B / 512 KB (51.84%) |
| Release | 132860 B / 2 MB (6.34%) | 271808 B / 512 KB (51.84%) |

> 两种配置警告数均为 **0**。RAM 占用来自 RAM_D1（AXI-SRAM，主 RAM）；ITCM/DTCM/
> RAM_D2/RAM_D3 当前未使用。总警告=0 已通过 `cmake --build` 全量编译验证。

### STemWin `GUI_Init` 卡死根因与修复（已解决）

`GUI_Init()` 在入口的保护逻辑里用 **硬件 CRC 外设** 做完整性校验：先写
`CRC_CR=1`（复位）与 `CRC_DR=0xf407a5c2`，再读回 `CRC_DR` 期望得到 `0xb5e8b5cd`，
不符则陷入死循环（`b.n` 自跳）。**本工程此前从未使能 CRC 时钟**，导致该校验
永远失败 → `GUI_Init` 死循环（表现即“卡死在 GUI_Init”）。

修复：在 `Application/app_main.c` 的 `GUI_Init()` 之前加
`__HAL_RCC_CRC_CLK_ENABLE();`（CRC 在 AHB4，`RCC->AHB4ENR` 的 `CRCEN`）。
已通过 OpenOCD+gdb 在真机验证：使能 CRC 时钟后 `GUI_Init` 正常返回，固件进入主循环。

---

## 7. 目录结构

```
.
├── Application/      # app_main.c / app_ui.c (emWin 版信息面板)
├── Bsp/
│   ├── drv_spi_oled.c/h      # ST7789 SPI6 驱动 + OLED_CopyBuffer
│   ├── drv_oled_fonts.c/h     # 编译进固件的 ASCII 点阵表
│   ├── drv_oled_text.c/h      # SD 中文点阵读取 (lcd_driver_get_hzmat)
│   ├── drv_sdio.c/h / drv_rtc.c/h / disk_interface.c
│   ├── lv_gbk_map.c/h         # Unicode -> GBK 映射表
│   └── emWin/                 # LCDConf / GUIConf / GUI_X / emwin_font_gbk
├── Core/  Drivers/  third_party/  (FatFs + STemWin)
├── cmake/  scripts/  .vscode/
├── CMakeLists.txt  build_oneclick.bat  openocd.cfg
└── sys_startup/     # 启动文件 / 设备头 / 链接脚本
                     #   (STM32H743ZITX_FLASH.ld, startup_stm32h743xx.s, system_stm32h7xx.c)
```
