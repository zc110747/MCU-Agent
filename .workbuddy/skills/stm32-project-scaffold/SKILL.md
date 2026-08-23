---
name: stm32-project-scaffold
description: STM32 嵌入式工程的统一骨架与构建系统规范：app/bsp/Drivers/third_party 分层、CMake+Ninja 交叉编译、OpenOCD 烧录、链接脚本、CMakePresets、VSCode Cortex-Debug 集成。适用于"搭建新 STM32 工程""规范化已有工程结构""配置 CMake/OpenOCD 工具链""修复链接脚本与烧录配置"。触发词：工程结构、目录分层、CMake 交叉编译、ninja、openocd 烧录、链接脚本、CMakePresets、Cortex-Debug、STM32 工程模板、startup 向量表。
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
├── Drivers/        CMSIS-Core / CMSIS-Device(ST) / STM32x HAL 驱动（ST 官方，不手改）
├── third_party/    第三方库（tinyusb / lvgl / FatFs / LwIP / FreeRTOS / mbedTLS）
├── ldscript/       stm32xxxx_flash.ld 链接脚本
├── cmake/          arm-none-eabi.cmake 交叉工具链文件
├── openocd/        *.cfg 烧录/调试配置（stlink 本板 / cmsisdap 目标板）
├── tools/          PC 端工具与 verify 脚本（python / C#）
├── .vscode/        c_cpp_properties.json / launch.json / tasks.json / settings.json / *.svd
├── CMakeLists.txt
└── CMakePresets.json   # 可选，推荐（debug/release/debug-hs 预设）
```

**铁律**：
- HAL 驱动只放 `Drivers/`，用户驱动放 `bsp/`，第三方库放 `third_party/`（统一管理，便于复用）。
- 调试/构建产物**一律相对路径**，不写死本机绝对路径（换机器即失效）。
- `third_party` 体积大，本项目打包在 `support_tools/env_support_for_stm32h743.zip`，
  Zephyr 另包 `env_support_for_zephyr.zip`，解压到工程即用。

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

## 三、链接脚本（.ld）边界必须实测

- 栈顶 `_estack` 与 `RAM LENGTH` 必须与芯片真实容量一致（ST 官方模板常带错尺寸）。
- **STM32F429IG 易错**：连续 SRAM 仅 192K（0x20000000~0x2002FFFF），CCM 64K @0x10000000
  不连续、ETH/DMA 访问不到。ST 模板常误写 256K/2048K。
- SDRAM 段（外部内存）必须 `(NOLOAD)`：startup 的 bss 清零在 FMC 初始化前执行，
  SDRAM 段若参与清零会 HardFault。
- 详细排障见 `stm32-bare-metal-bringup` 的"栈顶在真实 RAM 内"章节。

## 四、OpenOCD 烧录配置

`openocd/stm32h743_stlink.cfg`（烧写本板）：
```tcl
source [find interface/stlink.cfg]
transport select swd
source [find target/stm32h7x.cfg]
```
烧录命令：
```bash
openocd -f openocd/stm32h743_stlink.cfg -c "program build/debug/xxx.elf verify reset exit"
```
- OpenOCD 0.12 用 `transport select swd`（**不支持旧 `hla_swd`**）。
- 通过本板作探针访问目标时，另写 `stm32f429_cmsisdap.cfg`（`cmsis_dap` interface）。

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

## 六、VSCode Cortex-Debug 集成

`launch.json` 关键字段：
```json
{
  "executable": "build/debug/xxx.elf",
  "serverpath": "D:/Software/openocd/bin/openocd.exe",
  "searchDir": "D:/Software/openocd/share/openocd/scripts",
  "configFiles": ["openocd/stm32h743_stlink.cfg"],
  "device": "STM32H743ZI",
  "rtos": "auto",           // Zephyr 工程填 "Zephyr"
  "preLaunchTask": "build"  // F5 先编译再调试
}
```
- `tasks.json` 提供 `build`/`clean`/`flash` 等任务供 preLaunchTask 调用。
- `settings.json`：`cmake.useCMakePresets=always`，指定工具链/OpenOCD 路径。

## 七、startup 向量表（FreeRTOS 工程必改）

用 FreeRTOS V11 时，startup 的 SVC/PendSV/SysTick 必须**直指** port 函数（不能转发包装）：
```asm
.word vPortSVCHandler      /* SVCall */
.word xPortPendSVHandler   /* PendSV */
.word xPortSysTickHandler  /* SysTick */
```
详见 `stm32-bare-metal-bringup` 的"FreeRTOS 化 LwIP 移植实战坑"。
