---
name: stm32-project-scaffold
description: STM32 嵌入式工程的统一骨架与构建系统规范：app/bsp/Drivers/third_party 分层、CMake+Ninja 交叉编译、OpenOCD 烧录、链接脚本、CMakePresets、VSCode Cortex-Debug 集成，以及多工程 .vscode 批量统一（tasks.json 仅 configure/build/clean/flash、工具走 PATH 裸名、svd/cfg 放工程根）。适用于"搭建新 STM32 工程""规范化已有工程结构""配置 CMake/OpenOCD 工具链""修复链接脚本与烧录配置""批量统一多个工程的 .vscode"。触发词：工程结构、目录分层、CMake 交叉编译、ninja、openocd 烧录、链接脚本、CMakePresets、Cortex-Debug、.vscode 统一、tasks.json configure build clean flash、svd cfg 工程根目录、多工程批量统一、STM32 工程模板、startup 向量表、build_all 一键编译全部工程、support_all 支持包同步、README 工程说明文档、开发流程文档编写、工程 README 规范、CMSIS 启动文件 GLOB_RECURSE 禁止、HAL 目录 Cube 标准命名、startup_stm32h7xx 显式列出、sys_startup 本地设备层替代 Drivers/CMSIS/Device、build_oneclick 致命坑4 cd %~dp0 尾随反斜杠、.bat 纯英文、多缓冲防撕裂结构。
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
├── Drivers/        CMSIS-Core / CMSIS-Device(ST) / STM32x_HAL_Driver（ST 官方，不手改；HAL 用 Cube 标准子目录名，如 STM32H7xx_HAL_Driver）
├── third_party/    第三方库（tinyusb / lvgl / FatFs / LwIP / FreeRTOS / mbedTLS）
├── ldscript/       stm32xxxx_flash.ld 链接脚本
├── cmake/          arm-none-eabi.cmake 交叉工具链文件
├── openocd.cfg     OpenOCD 烧录/调试配置（stlink 本板 / cmsisdap 目标板），放工程根（与 .vscode 同级，不再用 openocd/ 子目录）
├── *.svd           MCU SVD 寄存器描述（如 STM32H743.svd / STM32F429.svd），放工程根，供 cortex-debug 加载
├── tools/          PC 端工具与 verify 脚本（python / C#）
├── .vscode/        c_cpp_properties.json / launch.json / tasks.json / settings.json
├── CMakeLists.txt
└── CMakePresets.json   # 可选，推荐（debug/release/debug-hs 预设）
```

**铁律**：
- HAL 驱动只放 `Drivers/`，用户驱动放 `bsp/`，第三方库放 `third_party/`（统一管理，便于复用）。
- 调试/构建产物**一律相对路径**，不写死本机绝对路径（换机器即失效）。
- `*.svd` / `*.cfg` 放**工程根目录**（与 `.vscode`、`CMakeLists.txt` 同级），**不再需要 `openocd/` 子目录**。
- `third_party` 体积大，建议把可复用的 `Drivers/` / `third_party/` 打包成压缩包随工程分发，
  解压即用（路径一律相对引用）。

## 二、CMake 交叉编译骨架

`cmake/arm-none-eabi.cmake`：
```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
```

顶层 `CMakeLists.txt` 关键项：
```cmake
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi.cmake)
set(MCU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")  # H7
# F4 用：-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=softfp
add_compile_options(-Wall -Wextra)            # 本项目强约束零警告
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -T${LINKER_SCRIPT} -Wl,--gc-sections")
# 关键：让 CMake 跟踪 .ld，否则改了链接脚本 ninja 报 no work to do
set_target_properties(${PROJECT_NAME}.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
```

**新增 `.c` 源文件必须重跑 `cmake` 重新 GLOB**（`file(GLOB app/*.c)` 在配置时展开并缓存，
ninja 增量不会自动重扫，否则新文件不进编译）。

### 2.1 CMSIS Device 启动文件必须显式列出（禁止 GLOB_RECURSE）

**坑（010.stm32h743_boot 真机教训）**：CMSIS-Device 树为每个 H7 型号都提供 startup 文件
（arm/gcc/iar 三套汇编器语法 × 几十个型号），还有多个 `system_*.c` 变体。若用
`file(GLOB_RECURSE ... "Drivers/CMSIS/Device/*.c" "*.s")`，会把约 60 个 startup.s + 多个
`system_*.c` 全收进来，导致：
- 重复定义 `Reset_Handler` / `SystemInit` 符号（multiple definition 链接错误）；
- iar/arm 语法的 `.s` 被 GCC 汇编器解析 → 汇编语法错误。

**正确做法——只列本芯片需要的两个文件**（Cube 标准路径）：
```cmake
set(CMSIS_DEVICE_DIR "${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx")
set(CMSIS_SOURCES
  ${CMSIS_DEVICE_DIR}/Source/Templates/gcc/startup_stm32h743xx.s
  ${CMSIS_DEVICE_DIR}/Source/Templates/system_stm32h7xx.c
)
```

### 2.2 HAL 源文件收集（用 Cube 标准子目录名）

HAL 用 Cube 标准子目录名 `Drivers/STM32H7xx_HAL_Driver/`（F4 对应 `Drivers/STM32F4xx_HAL_Driver/`），
不再用扁平的 `Drivers/HAL/`。整目录 GLOB 即可，未使用的 HAL 源会被 `-Wl,--gc-sections` 丢弃：
```cmake
file(GLOB HAL_SOURCES "Drivers/STM32H7xx_HAL_Driver/Src/*.c")
# 排除 CubeMX 模板文件（它们重定义 HAL_InitTick 等，导致符号冲突）
list(FILTER HAL_SOURCES EXCLUDE REGEX "[Tt]emplate")
```
对应 include 目录：`Drivers/STM32H7xx_HAL_Driver/Inc`。

## 三、链接脚本（.ld）边界必须实测

- 栈顶 `_estack` 与 `RAM LENGTH` 必须与芯片真实容量一致（ST 官方模板常带错尺寸）。
- **STM32F429IG 易错**：连续 SRAM 仅 192K（0x20000000~0x2002FFFF），CCM 64K @0x10000000
  不连续、ETH/DMA 访问不到。ST 模板常误写 256K/2048K。
- SDRAM 段（外部内存）必须 `(NOLOAD)`：startup 的 bss 清零在 FMC 初始化前执行，
  SDRAM 段若参与清零会 HardFault。
- 栈顶 `_estack` 与 `RAM LENGTH` 必须按芯片真实容量（ST 官方模板常带错尺寸，如 F429 误写
  256K/2048K，实际连续 SRAM 仅 192K）；否则内存越界 HardFault，详见本节上文边界实测。

## 四、OpenOCD 烧录配置（放工程根）

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
- 通过本板作探针访问目标（CMSIS-DAP v1 自研探针）时，另写 `openocd_cmsisdap.cfg`
  （`cmsis_dap` interface），也放工程根，并在 `launch.json` 多配一条 `attach` 配置。
- 缺 `openocd.cfg` 的工程按芯片补建：H7 用 `stm32h7x.cfg`，F4 用 `stm32f4x.cfg`；
  找不到 `[find ...]` 脚本或路径错误属配置错误，需修正；仅 "no probe attached" 类 adapter 报错属正常（需真机）。

## 五、CMakePresets（推荐）

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

## 六、VSCode Cortex-Debug 集成（裸工具名 + 根级 cfg/svd）

**工具链已加入系统 PATH，所有引用只写裸程序名**（不加安装目录、不带 `.exe`）：
- `launch.json`：`"openocdPath": "openocd"`、`"gdbPath": "arm-none-eabi-gdb"`
- `settings.json`：`"cortex-debug.openocdPath": "openocd"`、`"cortex-debug.gdbPath": "arm-none-eabi-gdb"`、`"C_Cpp.default.compilerPath": "arm-none-eabi-gcc"`
- `configFiles` / `svdFile` 用 `${workspaceFolder}` 指向**工程根**的 `openocd.cfg` / `*.svd`

`launch.json` 关键字段：
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
- ⚠️ **严禁**写死 `<openocd 安装目录>/bin/openocd.exe`、`${env:ARM_GNU_TOOLCHAIN_BIN}/...` 等本机/
  环境变量路径（换机器即失效，与「铁律：一律相对路径」冲突）。
- `tasks.json` 提供 `configure`/`build`/`clean`/`flash` 任务供 `preLaunchTask` 调用，详见第九节。

## 七、startup 向量表（FreeRTOS 工程必改）

用 FreeRTOS V11 时，startup 的 SVC/PendSV/SysTick 必须**直指** port 函数（不能转发包装）：
```asm
.word vPortSVCHandler      /* SVCall */
.word xPortPendSVHandler   /* PendSV */
.word xPortSysTickHandler  /* SysTick */
```
FreeRTOS 独占 SysTick 会导致裸机 HAL 时基（`HAL_Delay`）冻结，需在 `HAL_InitTick` 外另行提供
时基或改用定时器；LwIP/SDRAM 等网络工程实战坑见 `stm32-peripheral-drivers` 第八节（F4 网络）。

## 八、双固件镜像 Bootloader 工程约定

Bootloader + App 共存于同一片内部 Flash，需**两套独立构建 + 固定地址分区**：

- **内存布局（H743 2MB 双 Bank）**：
  | 区域 | 地址 | 说明 |
  |------|------|------|
  | Bootloader | `0x08000000` sec0 (128KB) | 主构建 `stm32h7_boot.elf` |
  | App 镜像 | `0x08020000` sec1-14 | 独立 CMake + `stm32h7_app.ld`（`ORIGIN=0x08020000`） |
  | 版本槽 | `0x08021000` (App 偏移 0x1000, 4B) | `.app_version` 固定段 |
  | 配置区 | `0x081E0000` sec15 (64B) | magic / len / version / hmac / crc32 |

- **两套独立 CMake + 链接脚本**：bootloader 与 app 各一个工程目录，app 的 ld `ORIGIN` 必须指向 `0x08020000`，链接脚本互相独立；改 `.ld` 后务必让 CMake 跟踪（`LINK_DEPENDS`，见第二节）。
- **升级包经 U 盘注入**：QSPI FatFs + TinyUSB MSC 把 QSPI 暴露为 PC U 盘；`verify.json`(name/len/HMAC/version) + 同名 `.bin` 落到根目录 → 复位后 Bootloader 校验 → 擦写 → 跳 App。
- **跳转前清环境**：见 `stm32-peripheral-drivers` 第九节的跳转序列；App 入口 `__enable_irq()` + `SysTick_Handler` 必备（否则 `HAL_Delay` 卡死）。
- **擦写引擎放 AXI SRAM**：见 `stm32-peripheral-drivers` 第九节 —— 绝不放 DTCM。
- **防砖**：任何校验失败在擦写前 abort，已运行 App 不被破坏。
- 验收方法见 `stm32-verification-acceptance` 第八、九节（含 gdb 直调 `BFLASH_ProgramBlock` 与 Flash 回读）。

## 九、多工程 .vscode 批量统一规范

仓库下有多个 STM32 工程（不同芯片 / 不同构建机制）需统一 `.vscode` 与仿真配置时，遵循以下铁律：

### 规则 1 — 工具走系统 PATH（裸程序名）
`cmake` / `ninja` / `openocd` / `arm-none-eabi-gcc` / `arm-none-eabi-gdb` 已加入系统 PATH。
所有引用只写**裸程序名**，不加安装目录、不带 `.exe`：
- `tasks.json`：`command: "cmake ..."`、`"openocd ..."`、`"arm-none-eabi-gdb ..."`
- `settings.json`：`"cortex-debug.openocdPath": "openocd"`、`"cortex-debug.gdbPath": "arm-none-eabi-gdb"`、`"C_Cpp.default.compilerPath": "arm-none-eabi-gcc"`
- `launch.json`：`"openocdPath": "openocd"`、`"gdbPath": "arm-none-eabi-gdb"`
- ❌ 严禁写死 `<openocd 安装目录>/bin/openocd.exe`、`${env:ARM_GNU_TOOLCHAIN_BIN}/...` 等本机/环境变量路径。

### 规则 2 — tasks.json 只保留 4 个任务
统一为 `configure` / `build` / `clean` / `flash`，其余（release / erase / reset / dap-test / serial /
OpenOCD server / 模型导出等）全部删除。
- `build` 设为默认构建组：`"group": {"kind":"build","isDefault":true}`，供 `preLaunchTask` 调用。
- `flash` 用 `"dependsOn": "build"` 先编译再烧录。
- **构建机制按工程现状保留**（下表），不要强行统一成一种：

| 工程类型 | configure | build | clean | flash 的 elf 路径 |
|---|---|---|---|---|
| 预设工程（有 CMakePresets.json） | `cmake --preset debug` | `cmake --build build/debug` | `cmake --build build/debug --target clean` | `build/debug/xxx.elf` |
| 普通 CMake（无预设） | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` | `cmake --build build` | `cmake --build build --target clean` | `build/xxx.elf` |
| Zephyr（west） | `python -m west build -b <board>/<soc> -d build -s .` | `python -m west build -d build` | `python -m west build -t clean -d build` | `build/zephyr/zephyr.elf` |

- ⚠️ 预设工程的 build 子目录（如 `build/debug`）必须与 CMakePresets 的 `binaryDir` 一致。
- ⚠️ 旧 `CMakeCache.txt` 路径不匹配（大小写 / 旧仓库路径）会导致 regenerate 失败；
  跑一次 `clean`（或删 `build/debug`）即可恢复——保留 `clean` 任务正是为此。

### 规则 3 — *.svd / *.cfg 放工程根目录（与 .vscode 同级），不再需要 openocd/ 子目录
- `openocd.cfg`（及可选的 `openocd_cmsisdap.cfg`）直接放工程根，与 `CMakeLists.txt` / `.vscode/` 同级。
- `*.svd`（如 `STM32H743.svd` / `STM32F429.svd`）也放工程根。
- `launch.json` 用 `${workspaceFolder}/openocd.cfg`、`${workspaceFolder}/STM32H743.svd` 引用。
- 原散落在 `openocd/`、`debug/`、`tools/` 下的同名文件应迁移到根并删除已清空的子目录。
- 缺失 `openocd.cfg` 的工程按芯片补建（H7：`stm32h7x.cfg`；F4：`stm32f4x.cfg`；均 `interface/stlink.cfg` + `transport select swd`）。
- 经自研 CMSIS-DAP 探针调目标板时，保留额外 `openocd_cmsisdap.cfg` + 一条 `attach` 配置（`cmsis_dap` interface）。

### 批量检索时排除 .gitignore
- 处理 / 搜索文件时，排除 `.gitignore` 中声明的路径（如 `third_party/`、`Drivers/STM32H7xx_HAL_Driver/`、`build/`），
  它们多是未检出的子模块 / 第三方库，缺失是正常的，不要当作错误。
- 校验脚本应只对「源码完整、可独立构建」的工程判定 PASS/FAIL，依赖缺失的标记为「环境依赖缺失」。
- 示例：`git -C <proj> check-ignore <path>` 可快速判定某缺失文件是否属被忽略项。

### 统一模板

`tasks.json`（4 任务，普通 CMake 示例；预设工程把命令换成 `cmake --preset debug` / `cmake --build build/debug`）：
```json
{
  "version": "2.0.0",
  "tasks": [
    {"label":"configure","type":"shell","command":"cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug","problemMatcher":[],"group":"build"},
    {"label":"build","type":"shell","command":"cmake --build build","group":{"kind":"build","isDefault":true},"problemMatcher":["$gcc"],"presentation":{"reveal":"always","panel":"shared","clear":true}},
    {"label":"clean","type":"shell","command":"cmake --build build --target clean","problemMatcher":[]},
    {"label":"flash","type":"shell","command":"openocd -f openocd.cfg -c \"program build/xxx.elf verify reset exit\"","dependsOn":"build","problemMatcher":[],"presentation":{"reveal":"always","panel":"shared"}}
  ]
}
```

`settings.json`：
```json
{
  "cmake.generator": "Ninja",
  "cmake.configureOnOpen": false,
  "cortex-debug.openocdPath": "openocd",
  "cortex-debug.gdbPath": "arm-none-eabi-gdb",
  "cortex-debug.armToolchainPath": "",
  "C_Cpp.default.compilerPath": "arm-none-eabi-gcc",
  "C_Cpp.default.intelliSenseMode": "gcc-arm",
  "cmake.useCMakePresets": "always"
}
```

`launch.json`（cortex-debug，根级 cfg/svd）：
```json
{
  "version": "0.2.0",
  "configurations": [
    {"name":"Debug (OpenOCD + ST-Link)","type":"cortex-debug","request":"launch","servertype":"openocd",
     "cwd":"${workspaceFolder}","executable":"${workspaceFolder}/build/debug/xxx.elf",
     "configFiles":["${workspaceFolder}/openocd.cfg"],"openocdPath":"openocd","gdbPath":"arm-none-eabi-gdb",
     "device":"STM32H743ZI","interface":"swd","runToEntryPoint":"main","preLaunchTask":"build",
     "showDevDebugOutput":"none","svdFile":"${workspaceFolder}/STM32H743.svd"},
    {"name":"Attach (no reflash)","type":"cortex-debug","request":"attach","servertype":"openocd",
     "cwd":"${workspaceFolder}","executable":"${workspaceFolder}/build/debug/xxx.elf",
     "configFiles":["${workspaceFolder}/openocd.cfg"],"openocdPath":"openocd","gdbPath":"arm-none-eabi-gdb",
     "device":"STM32H743ZI","interface":"swd","showDevDebugOutput":"none","svdFile":"${workspaceFolder}/STM32H743.svd"}
  ]
}
```

### 批量验证脚本思路（一次性，给出 pass/fail 计数）
- **仿真链路**：对每个工程 `openocd -f openocd.cfg -c "init; exit"`，预期「脚本/路径解析 OK」；
  仅缺物理探针的 adapter 报错属正常（需真机才能 flash）。
- **编译链路**：跑 `configure` + `build`，记录 PASS/FAIL + 警告数；`third_party`/`Drivers` 缺失
  （被 gitignore）导致的 FAIL 标记为「环境依赖缺失」，与「配置改动导致」区分开。

---

## 十、一键编译 bat 规范（每个工程根目录 `build_oneclick.bat`）

需求：① 全英文输出；② 先检查工具（ST 工程检查 `cmake/ninja/openocd/arm-none-eabi-gcc`，ESP 走各自
工具），ST 工程还要检查根目录 `Drivers`、`third_party`，缺失则提示从 `..\support_tools\env_support_for_*.zip`
解压；③ 流程 `configure → clean → build`（ESP 按自身流程：`arduino-cli compile` / `idf.py build`）；
④ 失败立即 `goto END` 中止；⑤ 所有出口（成功/失败/中止）都 `goto END → pause → exit /b %ERR%`，
双击即可停留查看错误。

**⚠️ 致命坑 1 —— 禁止用 `!MISSING!` 延迟展开打印缺失工具名**
旧写法先 `set "MISSING=!MISSING! cmake"` 累积、再 `echo ...:!MISSING!`。一旦该分支被触发且延迟展开未生效
（或 `setlocal enabledelayedexpansion` 未真正生效），`!MISSING!` 会被原样打印成 `:!MISSING!` 字面量。
正确做法：用 `for %%T` 循环逐个 `where %%T`，缺失时**直接 `echo` 出该工具名**，再用 `set "TOOLMISS=0/1"`
标记，循环结束后在**顶层**用 `%TOOLMISS%` 判定（顶层 `%VAR%` 在执行时展开，无需延迟展开）：

```bat
set "TOOLMISS=0"
for %%T in (cmake ninja openocd arm-none-eabi-gcc) do (
    where %%T >nul 2>&1
    if errorlevel 1 (
        echo [ERROR] Required tool not found: %%T
        set "TOOLMISS=1"
    )
)
if not "%TOOLMISS%"=="0" (
    echo         Please refer to document/support.md for installation instructions.
    set "ERR=1"
    goto END
)
```

**⚠️ 致命坑 2 —— 用 Python 生成 .bat 时 `%` 格式化会把 `%%` 吞成 `%`**
Python `"for %%T in (%s)" % tools` 中 `%%` 是 `%` 转义，结果会变成单 `%T`，导致 `for` 循环语法错误。
生成该行的正确写法：避开 `%` 格式化，用字符串拼接：
`"for %%T in (" + " ".join(tools) + ") do ("`（其余 `%%T` 在普通字符串里保留为 `%%`，不受影响）。

构建命令与已验证的 `tasks.json` 完全一致：预设工程 `cmake --preset debug` + `build/debug`；
普通 CMake `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` + `build`；Zephyr `west build`；
ESP32 各自流程。结尾判定 `if %ERR%==0 ( ... ) else ( ... )` + `:END` + `pause` + `exit /b %ERR%`。

**⚠️ 致命坑 3 —— `echo`/`REM` 行里的 `& ( ) | < >` 在 cmd 下全部危险，必须清除**
- `(` 出现在 `echo` 参数**行首**（首个非空格字符）会被当成命令组起始：`echo (or copy ...)` → 遇到 `)` 提前结束，
  后面 `into this folder` 变成非法命令，报 `此时不应有 into。`。
- `&` 是命令分隔符：`echo [STEP] Configure & Build ...` → `&` 把行拆成两条命令，后半段 `Build ...` 被当成
  外部命令执行，报 `'Build' 不是内部或外部命令`。
- `( ) | < >` 同理会在 `echo`/`REM` 文本里被 cmd 解析。
正确做法：生成脚本时对**所有 `echo`/`REM` 行**做清洗，把 `&`→`and`、`| < > ( )`→删除（仅清洗这两类行，
`if/for/cmake` 等命令行的括号必须保留）。示例清洗函数（Python）：

```python
def sanitize(line):
    s = line.lstrip()
    if s.startswith("echo") or s.startswith("REM"):
        return (line.replace("&","and").replace("|"," ").replace("<"," ")
                    .replace(">","").replace("(","").replace(")",""))
    return line
```

反斜杠 `\`（如 `..\support_tools\...` 路径）在 cmd 的 `echo` 中是字面量，**不受影响**，无需处理。

**⚠️ 致命坑 4 —— `cd /d "%~dp0"` 尾随反斜杠 + `for` 块内 `2>&1` 触发 `此时不应有 .`**
`%~dp0` 永远带尾随 `\`，`cd /d "D:\...\009.stm32h743_zephyr\"` 行尾 `\"` 被 cmd 当成转义引号 → 引号未闭合 →
后续 `echo` 行被吞 → 遇路径里的 `.` 报 `此时不应有 .。`；且 `for %%T in (...) do ( where %%T >nul 2>&1 ... )`
括号块内 `2>&1` 的 `&` 被当成命令分隔符解析报错。
正确写法：先去尾随 `\` 再 `cd`；`2>&1` 改成 `> nul 2>nul`：
```bat
set "SD=%~dp0"
if "%SD:~-1%"=="\" set "SD=%SD:~0,-1%"
cd /d "%SD%"
for %%T in (cmake ninja openocd arm-none-eabi-gcc) do ( where %%T > nul 2>nul )
```
> 该 `build_oneclick.bat` 坑已在 `009.stm32h743_zephyr` 真机复现并修复，沉淀于此避免新工程重蹈。

**⚠️ 所有 `.bat` 文件必须纯英文（不含任何中文注释）**：GBK 控制台解析中文注释会乱码甚至语句截断，
生成/手写 `.bat` 时一律用英文注释或干脆无注释。

---

## 十一、工作空间级一键编译全部工程 `build_all.bat`

需求：在**仓库根目录**（所有工程的上一层）放一个脚本，一键顺序编译全部工程；**某工程失败暂停等你回车后继续，直到全部完毕**。

- **位置**：仓库根 `build_all.bat`，与 `001.*`/`003.*`/... 各工程目录同级（不是某个工程内部）。
- **核心逻辑**：`for %%P in (工程列表)` → `call "%~dp0<proj>\build_oneclick.bat" < nul`。
  - 子脚本末尾 `pause` 经 `< nul` 喂 EOF 立即返回（不再阻塞），由父脚本统一掌控暂停时机。
  - **出错才暂停**：某工程 `exit /b` 非 0 → 打印 `[ERROR]`，`pause` 等用户回车后继续后续工程；成功直接继续（不打断）。
  - 缺 `build_oneclick.bat` 的工程 → 标记 `[SKIP]`，不报错中断整体流程。
  - 末尾输出汇总 `Passed / Failed / Skipped` 并 `pause` 等待回车（避免双击后窗口直接关闭、丢失结果）。
- **cmd 老坑（必看）**：
  - `for` 循环内**不能用 `goto`**（会直接中断整个循环）→ 改用 `call :build` 子例程，循环体只写 `call :build "%%P"`，所有 `goto` 放在子例程内。
  - 计数器在 `for`/`call` 内多次累加需延迟展开 → 顶部 `setlocal enabledelayedexpansion`，计数用 `!VAR!`。
  - 子脚本 `cd /d "%~dp0"` 在 `setlocal` 作用域内，返回后不影响父脚本 CWD（父脚本用 `%~dp0` 绝对路径调用，安全）。
- **验证**：① dummy 三态（成功/失败/缺失）确认 `< nul` 跳过子 pause、`errorlevel` 正确回传、失败暂停后继续、计数正确；② 真实 `001` 工程 `call ... < nul` 实测：`cmake` configure→clean→build 全过，产出 `h743_tinyusb_cdc.elf`（FLASH 65668B/2MB≈3.13%），`ERRLEV=0`，证明 `< nul` 不干扰 cmake/ninja 且退出码正确回传。

> 单工程脚本规范见第十节；本节的 `build_all.bat` 是其在工作空间级的编排层，二者配套使用。

---

## 十二、工程 README 说明文档规范

每个工程根目录 `README.md` 应**随代码同步更新**，让 AI / 人工一眼看懂「做什么 / 怎么搭 / 怎么调 / 怎么验」。建议固定章节：

1. **项目概述 / 处理内容**：一句话目标 + 功能范围（例：001=CDC+MSC 复合设备；003=LVGL 中文 OLED）。
2. **硬件与接口**：芯片、HSE 时钟、关键外设引脚（USB/显示/SPI/USART）、调试方式（SWD+ST-Link、SVD 文件）。
3. **工程结构**：目录树，标注 `app/bsp`/`Drivers`/`third_party` 分层与关键文件（`openocd.cfg`、链接脚本、`.svd` 置根）。
4. **开发流程**：分步实现建议（先 X 再 Y）与来源（指向 `prompter.md`）。
5. **构建与运行**：
   - 单工程 `build_oneclick.bat`、全仓库 `build_all.bat` 的使用位置与行为；
   - 手动命令（预设工程 `cmake --preset debug`；普通 CMake `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`），产物 elf 路径；
   - 依赖缺失处理（从 `..\support_tools\env_support_for_*.zip` 解压 `Drivers`/`third_party`）；
   - 零警告约束 + **实测资源占比（FLASH/RAM 数字显式列出）**。
6. **调试与烧录**：`.vscode/launch.json`（裸工具名 + 根级 cfg/svd + `preLaunchTask`）、命令行 `openocd -f openocd.cfg -c "program <elf> verify reset exit"`、单步/F5、gdb 脚本。
7. **验收与自测**：验收标准 + 自测脚本（Python `test_*.py` / `verify_*.py` 给出 pass/fail 计数）。
8. **常见问题**：表格列出典型坑与根因/处理（依赖缺失、`.ld` 改后 ninja `no work to do`、attach 超时、编码乱码等）。

**铁律**：路径一律相对；构建命令必须与 `tasks.json` / `build_oneclick.bat` 一致；资源占比、警告数等以**数字**显式给出；每次代码/配置变更同步更新 README。

---

## 十三、支持包同步脚本 `support_all.bat`

需求：在**仓库根目录**放一个脚本，把 `support_tools/` 下的支持包（`Drivers`/`third_party`/Zephyr 源码等）按需同步进各工程——目录不存在就解压 zip、逐条目对比、目标缺失才拷贝、已存在则跳过。

- **位置**：仓库根 `support_all.bat`（与各工程目录同级）。
- **工程→支持包映射**（按工程目录名判定）：
  - 目录名含 `zephyr` → `env_support_for_zephyr`（最高优先，覆盖前缀规则）
  - 首字符为 `0` → `env_support_for_stm32h743`
  - 首字符为 `1` → `env_support_for_stm32f429`
  - `2` 开头（ESP32）等无对应包 → 不处理（`for /d %%P in (0* 1*)` 只匹配 0/1 前缀工程）
- **三步逻辑**：
  1. 判定 `support_tools/env_support_for_XXX` 目录是否存在；不存在 → 解压 `support_tools/env_support_for_XXX.zip` 到 `support_tools/`（三个 zip 内部均包一层同名根目录，解压即得到该目录）。
  2. 解压方式：`tar -xf "<zip>" -C "<support_tools>"`（Windows 自带 tar/bsdtar，优先）；失败回退 `powershell -Command "Expand-Archive -Path '<zip>' -DestinationPath '<support_tools>' -Force"`。
  3. 逐个比对解压目录的**顶层条目**（如 `Drivers`/`third_party`/`zephyr`）：目标工程不存在该条目 → `robocopy "<pkg>\<item>" "<proj>\<item>" /E`（目录/文件通用，全有则整项拷贝）；已存在 → 打印 `[SKIP] <proj>\<item> already exists, skip.` 跳过。**不合并、不覆盖已有条目**（按名称全有/全无判断）。
- **cmd 老坑（沿用第十节·致命坑 3）**：`echo`/`REM` 行里的 `(` `)` `&` `|` `<` `>` 会被 cmd 解析 → 一律清洗（`&`→`and`，其余删除），命令行的括号保留。本脚本输出行已规避特殊字符。
- **幂等安全**：重复运行只补缺失项；已存在的目录/文件原样保留，不会误删或覆盖。
- **验证**：用临时夹具（含「仅 zip 的 h743」「预解压的 f429/zephyr」「缺失/已存在条目工程」「2xx 跳过」）跑通四分支——h743 触发解压+拷贝、zephyr 走命名分支、f429 已有条目 SKIP+缺失 COPY、201 不被处理；产物树与打印均符合预期。

> 本脚本与第十节 `build_oneclick.bat`、第十一节 `build_all.bat` 组成「依赖补齐 → 单工程编译 → 全工程编译」的完整工具链。

---

## 十四、sys_startup 本地设备层约定（强约束，替代 Drivers/CMSIS/Device）

所有 STM32H7 CMake 工程统一用本地 `sys_startup/` 取代 `Drivers/CMSIS/Device/ST/STM32H7xx/`
（已迁移并 Debug+Release 双构验证的工程：002/003/004/005/006/007/008/010；模板取自 `001/sys_startup`）。

### 14.1 目录树（禁止在 Drivers/ 下放 CMSIS-Device）
```
<project>/
├── sys_startup/                  # 本地设备层（替代 Drivers/CMSIS/Device）
│   ├── stm32h743xx.h             # 设备头
│   ├── stm32h7xx.h               # 系列头
│   ├── system_stm32h7xx.h/.c     # 系统时钟
│   ├── startup_stm32h743xx.s     # gcc 启动文件（arm/iar 同目录备用）
│   └── STM32H743ZITX_FLASH.ld    # 链接脚本（各工程自有，ldscript/ 作废）
├── Drivers/                      # 只放 CMSIS-Core / Include / DSP / NN + STM32x_HAL_Driver
├── app/ bsp/ third_party/
└── CMakeLists.txt
```

### 14.2 铁律
- 设备层一律放本地 `sys_startup/`，**禁止依赖 `Drivers/CMSIS/Device/ST/STM32H7xx/`**。
- **验收红线**：`grep -c "Drivers/CMSIS/Device" CMakeLists.txt` 出现次数 == 0；`sys_startup` 引用 ≥ 3 处。
- `c_cpp_properties.json` 的 includePath 从 `Drivers/CMSIS/Device/ST/STM32H7xx/Include` 改为 `sys_startup`。
- 迁移时删除旧位置（`Core/Src/system_stm32h7xx.c`、`startup/`、`Core/Startup/` 中的 startup/system 副本）。

### 14.3 CMakeLists 引用写法
```cmake
set(SYS_STARTUP_DIR ${CMAKE_SOURCE_DIR}/sys_startup)
include_directories(${SYS_STARTUP_DIR})
set(STARTUP_SOURCES
  ${SYS_STARTUP_DIR}/startup_stm32h743xx.s
  ${SYS_STARTUP_DIR}/system_stm32h7xx.c
)
# 链接脚本随工程：除 007 用 stm32h743zi_flash.ld 外，H743 多数为 STM32H743ZITX_FLASH.ld
set(LINKER_SCRIPT ${SYS_STARTUP_DIR}/STM32H743ZITX_FLASH.ld)
set_target_properties(${PROJECT_NAME}.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
```

### 14.4 新工程落地 5 步
1. 拷贝 `001/sys_startup/` 作模板；2. 选对应 `.ld`（见 14.3 注释）；3. CMakeLists 改引用；
4. `c_cpp_properties.json` includePath 改 `sys_startup`；5. 编译验证 `Drivers/CMSIS/Device` 出现次数 == 0。

