---
name: stm32-project-scaffold
description: STM32 嵌入式工程的统一骨架与构建系统规范：app/bsp/Drivers/third_party 分层、CMake+Ninja 交叉编译、**HAL 库按需引入（禁止整包导入）**、OpenOCD 烧录、链接脚本、CMakePresets、VSCode Cortex-Debug 集成、sys_startup 本地设备层（替代 Drivers/CMSIS/Device）、LVGL 多页面 UI 拆分约定，以及多工程 .vscode 批量统一（tasks.json 仅 configure/build/clean/flash、工具走 PATH 裸名、svd/cfg 放工程根）。
agent_created: true
---

# STM32 工程骨架与构建系统规范

统一的工程分层，让 AI Agent 在任意 STM32 项目里都能直接定位代码、增量编译、烧录调试。
本 skill 与 `stm32-ai-dev-environment`（环境）、`stm32-verification-acceptance`（验收）配套。

## 一、标准目录分层（强约束）

```
<project>/
├── app/            应用逻辑（main.c、业务模块、FreeRTOS/LwIP 移植、shell）
├── bsp/            用户开发的板级驱动（bsp_uart / bsp_led / bsp_i2c / bsp_sdram ...）
├── Drivers/        CMSIS-Core / CMSIS-Include / DSP / NN（ST 官方，不手改）；
│                   **禁止**放 CMSIS-Device(ST)（见 reference `sys-startup.md`）
├── sys_startup/    本地设备层（强约束）：device 头 + system_stm32h7xx.c
│                   + startup(gcc/arm/iar) + 本工程链接脚本 *.ld
├── third_party/    第三方库（tinyusb / lvgl / FatFs / LwIP / FreeRTOS / mbedTLS）
├── cmake/          交叉工具链文件 arm-none-eabi.cmake
├── openocd.cfg     OpenOCD 烧录/调试配置（stlink 本板 / cmsisdap 目标板），
│                   放工程根（与 .vscode 同级，**不用** openocd/ 子目录）
├── *.svd           MCU SVD 寄存器描述（如 STM32H743.svd），放工程根，供 cortex-debug 加载
├── tools/          PC 端工具与 verify 脚本（python / C#）
├── .vscode/        c_cpp_properties.json / launch.json / tasks.json / settings.json
├── CMakeLists.txt
└── CMakePresets.json   # 可选，推荐（debug/release/debug-hs 预设）
```

**铁律**：
- HAL 驱动只放 `Drivers/`，用户驱动放 `bsp/`，第三方库放 `third_party/`（统一管理，便于复用）。
- **设备层（startup 向量表 / `system_*.c` / device 头文件 / 链接脚本）一律放在工程本地
  `sys_startup/`，禁止依赖 `Drivers/CMSIS/Device/ST/...` 那棵庞大的官方树**（详见 `sys-startup.md`）。
- 调试/构建产物**一律相对路径**，不写死本机绝对路径（换机器即失效）。
- `*.svd` / `*.cfg` 放**工程根目录**（与 `.vscode`、`CMakeLists.txt` 同级），**不再需要 `openocd/` 子目录**。
- `third_party` 体积大，建议把可复用的 `Drivers/` / `third_party/` 打包成压缩包随工程分发，
  解压即用（路径一律相对引用）。

## 二、构建系统（CMake 交叉编译 + 源集切分 + 设备层）

三段要点，细节见对应 reference：

| 主题 | 一句话 | 详见 |
|---|---|---|
| 交叉编译骨架 | `cmake/arm-none-eabi.cmake` + MCU_FLAGS + `LINK_DEPENDS` 跟踪 `.ld` | `cmake-cross-build.md` |
| **HAL 按需引入** | **禁止 `GLOB` 整目录收集 HAL**，按 `HAL_XXX_` 反查模块；H7 侧**必带 `_ex.c`** 的模块：RCC/FLASH/DMA/TIM/PWR | `cmake-cross-build.md` |
| 设备层 | `sys_startup/` 替代 `Drivers/CMSIS/Device`；`grep -c "Drivers/CMSIS/Device" CMakeLists.txt` 必须为 0 | `sys-startup.md` |
| 链接脚本 | 栈顶 `_estack` / `RAM LENGTH` 按芯片真实容量；外部内存段必须 `(NOLOAD)` | `linker-script.md` |

- **`add_executable` 的 startup / `system_*.c` 必须显式列出**（禁止 `GLOB_RECURSE` 整树收集）：
  CMSIS-Device 树含数十个型号 × gcc/arm/iar 三套语法的 startup，全收会重复定义
  `Reset_Handler` / `SystemInit`，且把 iar/arm 语法的 `.s` 喂给 GCC 汇编器直接语法错。
- **新增 `.c` 源文件必须重跑 `cmake` 重新 GLOB**（`file(GLOB ...)` 在配置期展开并缓存，
  ninja 增量不会自动重扫，否则新文件不进编译）。
- ⚠️ 引入**新外设**时，必须把对应 `stm32h7xx_hal_<模块>.c`（及 `_ex.c`）同时补进
  **CMake 源集**与 **MDK-ARM uvprojx 的 HAL 组**（见 `stm32-keil-port`），否则链接报
  `undefined reference to HAL_XXX_Init`。

## 三、OpenOCD 烧录配置（放工程根）

`openocd.cfg`（烧写本板，**放工程根，不放 openocd/ 子目录**）：
```tcl
source [find interface/stlink.cfg]
transport select swd
source [find target/stm32h7x.cfg]
```
烧录命令：
```bash
openocd -f openocd.cfg -c "program build/debug/xxx.elf verify reset exit"
```
- OpenOCD 0.12 用 `transport select swd`（**不支持旧 `hla_swd`**）。
- 通过本板作探针访问目标（如自研 CMSIS-DAP v1 探针）时，另写 `openocd_cmsisdap.cfg`
  （`cmsis_dap` interface），也放工程根，并在 `launch.json` 多配一条 `attach` 配置。
- 缺 `openocd.cfg` 的工程按芯片补建：H7 用 `stm32h7x.cfg`，F4 用 `stm32f4x.cfg`。
  找不到 `[find ...]` 脚本或路径错误属配置错误，需修正；仅 "no probe attached" 类
  adapter 报错属正常（需真机）。

## 四、CMakePresets（推荐）

提供 `debug` / `release` / `debug-hs` 预设，省去重复 `-D`：
```json
{
  "configurePresets": [
    {"name":"debug","cacheVariables":{"CMAKE_BUILD_TYPE":"Debug"}},
    {"name":"release","cacheVariables":{"CMAKE_BUILD_TYPE":"Release"}},
    {"name":"debug-hs","cacheVariables":{"CMAKE_BUILD_TYPE":"Debug","USB_PORT":"HS"}}
  ]
}
```
构建：`cmake --preset debug && cmake --build build/debug`。
⚠️ 预设工程的 build 子目录（如 `build/debug`）必须与 CMakePresets 的 `binaryDir` 一致，
否则 `tasks.json` 的 `build`/`flash` 路径对不上。

## 五、VSCode Cortex-Debug 集成（裸工具名 + 根级 cfg/svd）

**工具链已加入系统 PATH，所有引用只写裸程序名**（不加安装目录、不带 `.exe`）：
- `launch.json`：`"openocdPath": "openocd"`、`"gdbPath": "arm-none-eabi-gdb"`
- `settings.json`：`"cortex-debug.openocdPath": "openocd"`、
  `"cortex-debug.gdbPath": "arm-none-eabi-gdb"`、
  `"C_Cpp.default.compilerPath": "arm-none-eabi-gcc"`
- `configFiles` / `svdFile` 用 `${workspaceFolder}` 指向**工程根**的 `openocd.cfg` / `*.svd`

```json
{
  "executable": "${workspaceFolder}/build/debug/xxx.elf",
  "configFiles": ["${workspaceFolder}/openocd.cfg"],
  "openocdPath": "openocd",
  "gdbPath": "arm-none-eabi-gdb",
  "device": "STM32H743ZI",
  "interface": "swd",
  "rtos": "auto",           // Zephyr 工程填 "Zephyr"
  "svdFile": "${workspaceFolder}/STM32H743.svd",
  "preLaunchTask": "build"  // F5 先编译再调试
}
```
- ⚠️ **严禁**写死 `<openocd 安装目录>/bin/openocd.exe`、`${env:ARM_GNU_TOOLCHAIN_BIN}/...`
  等本机/环境变量路径（换机器即失效）。
- 多工程批量统一（tasks.json 只留 `configure`/`build`/`clean`/`flash`、构建机制按工程现状保留、
  批量检索排除 `.gitignore`）见 `vscode-multi-project.md`。

## 六、startup 向量表（FreeRTOS 工程必改）

用 FreeRTOS V11 时，startup 的 SVC/PendSV/SysTick 必须**直指** port 函数（不能转发包装）：
```asm
.word vPortSVCHandler      /* SVCall */
.word xPortPendSVHandler   /* PendSV */
.word xPortSysTickHandler  /* SysTick */
```
FreeRTOS 独占 SysTick 会导致裸机 HAL 时基（`HAL_Delay`）冻结，需在 `HAL_InitTick` 外另行
提供时基或改用定时器（常见做法：HAL timebase 改到 TIM7）。

## 七、双固件镜像 Bootloader 工程约定

Bootloader + App 共存于同一片内部 Flash，需**两套独立构建 + 固定地址分区**：

- **内存布局（以 H743 2MB 双 Bank 为例）**：

  | 区域 | 地址 | 说明 |
  |------|------|------|
  | Bootloader | `0x08000000` sec0 (128KB) | 主构建 `*_boot.elf` |
  | App 镜像 | `0x08020000` sec1-14 | 独立 CMake + 独立 `.ld`（`ORIGIN=0x08020000`） |
  | 版本槽 | `0x08021000`（App 偏移 0x1000, 4B） | `.app_version` 固定段 |
  | 配置区 | `0x081E0000` sec15 (64B) | magic / len / version / hmac / crc32 |

- **两套独立 CMake + 链接脚本**：bootloader 与 app 各一个工程目录，app 的 ld `ORIGIN` 必须
  指向 App 基址，链接脚本互相独立；改 `.ld` 后务必让 CMake 跟踪（`LINK_DEPENDS`，见第二节）。
- **升级包可经 U 盘注入**：QSPI FatFs + TinyUSB MSC 把 QSPI 暴露为 PC U 盘；
  `verify.json`(name/len/HMAC/version) + 同名 `.bin` 落到根目录 → 复位后 Bootloader 校验
  → 擦写 → 跳 App。
- **跳转前清环境**与**擦写引擎放 AXI SRAM（绝不放 DTCM）**：见
  `stm32-peripheral-drivers/references/h7-flash-bootloader.md`。
- **防砖**：任何校验失败在擦写前 abort，已运行 App 不被破坏。
- 验收方法见 `stm32-verification-acceptance`（gdb 直调编程函数与 Flash 回读）。

## 八、多工程一键脚本族（编排层）

多工程仓库的脚本分工固定为三层，**完整配方（含可直接抄的 `.bat` 与全部致命坑）见
`references/oneclick-scripts.md`**：

| 脚本 | 位置 | 职责 |
|---|---|---|
| `<proj>/build_oneclick.bat` | 每个工程根 | 检查工具与依赖 → `configure → clean → build`，失败即中止，所有出口 `pause` + `exit /b %ERR%` |
| `<repo>/build_all.bat` | 仓库根（各工程上一层） | `call` 各工程脚本顺序编译；出错才 `pause` 继续；末尾汇总 `Passed/Failed/Skipped` |
| `<repo>/support_all.bat` | 仓库根 | 按工程类型把 `support_tools/` 下的支持包（`Drivers`/`third_party`/…）解压并补齐缺失条目（已有则 `[SKIP]`，不覆盖） |

三条最容易踩的（详见 reference，**不要凭记忆手写**）：
- `.bat` 一律**纯 ASCII + CRLF**，且 `echo`/`REM` 文本里的 `& ( ) | < >` 必须清洗——
  这是 `.bat` 报 `此时不应有 .` 类错误的**第一嫌疑**。
- `cd /d "%~dp0"` 的尾随反斜杠问题：正确写法是先去尾部 `\` 再 `cd`。
- `for` 循环块内**不能用 `goto`**（会中断整循环）→ 改 `call :子例程`；块内 `2>&1` 写 `2>nul`。

> `.bat` 的通用坑清单（27 条）在外层 skill `soc-debug-verification/references/bat-pitfalls.md`，
> 本 reference 只讲 ST 工程脚本特有的部分。

## 九、README 与 LVGL 多页面 UI 约定

- **工程 README**：固定 8 章（概述 / 硬件接口 / 工程结构 / 开发流程 / 构建运行 / 调试烧录 /
  验收自测 / 常见问题），随代码同步更新；资源占比、警告数一律以**数字**显式给出。
  细节见 `readme-convention.md`。
- **LVGL 多页面 UI**：页面超过 2~3 页时拆成 `app/app_ui.c` 框架 + `app/ui/page_*.c` 每页独立，
  共享声明集中在 `ui_common.h`；`CMakeLists.txt` 必须 `GLOB app/ui/*.c` 并重跑 cmake。
  细节（含 static/extern 冲突、切页防野指针）见 `lvgl-multi-page-ui.md`。

## 十、本 skill 的参考文件（按需读）

- `references/cmake-cross-build.md` — CMake 交叉编译骨架 + **HAL 按需引入** + CMSIS 源集切分
- `references/sys-startup.md` — 自研 `sys_startup/` 本地设备层约定（替代 `Drivers/CMSIS/Device`）
- `references/linker-script.md` — 链接脚本与内存边界（各 STM32 型号）
- `references/oneclick-scripts.md` — 一键编译/烧录脚本范式（`build_oneclick.bat` 等）
- `references/vscode-multi-project.md` — 多工程 `.vscode` 批量统一（Cortex-Debug launch/tasks）
- `references/readme-convention.md` — README 结构约定与增量汇报模板
- `references/lvgl-multi-page-ui.md` — LVGL 多页面 UI 组织范式
