# CMake 交叉编译骨架 + 源集切分（含 HAL 按需引入）

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
add_compile_options(-Wall -Wextra)            # 强约束零警告
set(CMAKE_EXE_LINKER_FLAGS "${MCU_FLAGS} -T${LINKER_SCRIPT} -Wl,--gc-sections")
# 关键：让 CMake 跟踪 .ld，否则改了链接脚本 ninja 报 no work to do
set_target_properties(${PROJECT_NAME}.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
```

**新增 `.c` 源文件必须重跑 `cmake` 重新 GLOB**（`file(GLOB app/*.c)` 在配置时展开并缓存，
ninja 增量不会自动重扫，否则新文件不进编译）。

### 1. CMSIS Device 启动文件必须显式列出（禁止 GLOB_RECURSE）

> **演进提示**：本节适用于仍保留官方树 `Drivers/CMSIS/Device/ST/...` 的**存量工程**。
> **新工程一律走 `sys_startup/` 本地设备层**（见 `references/sys-startup.md`；禁止依赖 `Drivers/CMSIS/Device`）。
> 两者共同铁律一致：**startup / `system_*.c` 必须显式列出，禁止 `GLOB_RECURSE` 整树收集**。

**真机教训**：CMSIS-Device 树为每个 H7 型号都提供 startup 文件
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

### 2. HAL 源文件按需引入（强约束，禁止整包导入）

> **本节是 HAL 按需引入的唯一主副本。** `stm32-keil-port` 只讲 Keil 侧的对应写法，
> 两侧源集必须一致（验收红线见文末）。

**铁律：不要完整导入 HAL 库，只引入本工程实际用到的 HAL 模块。** 不要用
`file(GLOB HAL_SOURCES "Drivers/STM32H7xx_HAL_Driver/Src/*.c")` 整目录收集——那会配置阶段编译
全部 80+ 个 HAL `.c`，拖慢构建、且与 Keil 源集不一致（Keil 全量入组会真链进去，Flash 虚高）。

**做法——从工程实际调用的 HAL 函数反查所需模块**：
```bash
grep -rhoE "\bHAL_[A-Za-z0-9_]+\(" app bsp Core 2>/dev/null \
  | sed -E 's/HAL_([A-Za-z0-9]+)_.*/\1/' | sort -u
```
再映射成文件名（`stm32h7xx_hal_<小写模块>.c`），注意这些**同一文件 / 必带 `_ex.c`** 的特例：
- `GPIO`→`hal_gpio.c`；`UART`/`USART`→`hal_uart.c`（同一文件）
- `RCC`→`hal_rcc.c` + `hal_rcc_ex.c`；`FLASH`→`hal_flash.c` + `hal_flash_ex.c`
- `DMA`→`hal_dma.c` + `hal_dma_ex.c`；`TIM`→`hal_tim.c` + `hal_tim_ex.c`
- `PWR`→`hal_pwr.c` + `hal_pwr_ex.c`（LDO/SMPS 配置依赖）

**任何工程都必须包含的 4 个基础件**：`stm32h7xx_hal.c`、`stm32h7xx_hal_cortex.c`、
`stm32h7xx_hal_rcc.c`、`stm32h7xx_hal_rcc_ex.c`（`hal_msp.c`/`hal_it.c` 属用户 `Core/Src`，照常加入）。

HAL 目录用 Cube 标准命名 `Drivers/STM32H7xx_HAL_Driver/`（F4 对应 `Drivers/STM32F4xx_HAL_Driver/`），
include 路径为 `.../Inc`（+ `.../Inc/Legacy`），**不变**。示例：
```cmake
set(HAL_DIR ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src)
set(HAL_SOURCES
  ${HAL_DIR}/stm32h7xx_hal.c  ${HAL_DIR}/stm32h7xx_hal_cortex.c
  ${HAL_DIR}/stm32h7xx_hal_rcc.c  ${HAL_DIR}/stm32h7xx_hal_rcc_ex.c
  ${HAL_DIR}/stm32h7xx_hal_gpio.c
  # ...其余按实际外设追加（连同对应 _ex.c）
)
# 不再 GLOB，也不再需要 list(FILTER ... EXCLUDE REGEX "[Tt]emplate")
```
⚠️ 引入**新外设**时，必须把对应 `stm32h7xx_hal_<模块>.c`（及 `_ex.c`）同时补进 **CMake 源集**与
**MDK-ARM 的 uvprojx HAL 组**（见 `stm32-keil-port` 的「HAL 库按需引入」节），否则链接报
`undefined reference to HAL_XXX_Init`。

**验收红线**：HAL 入组 `.c` 应为**个位数~十几个**（非目录全量 ~80+）；
`grep -c "hal_" CMakeLists.txt` 应等于 uvprojx 中 HAL 组文件数。
