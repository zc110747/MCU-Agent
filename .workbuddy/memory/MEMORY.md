# MCU-Agent 项目长期约定

## 双工具链（CMake/GCC + Keil MDK-ARM）源集铁律

所有 STM32 工程同时维护 `CMakeLists.txt`（arm-none-eabi-gcc）与 `MDK-ARM/*.uvprojx`（ARMCLANG），
两侧源集**必须严格对齐**。三条硬约束：

1. **禁止把 `syscalls.c` 加入 uvprojx**
   - `Core/Src/syscalls.c` 是 GCC/newlib 专用垫片（`_sbrk`/`_write`/`__io_putchar`），引用 `<sys/stat.h>`。
   - ARMCLANG 自带堆管理 → 加入必报 `_sbrk 未定义` / POSIX 头找不到。
   - **GCC 侧保留，Keil 侧排除；不改写该文件本身。**

2. **必须保留 `MDK-ARM/mdk_target.c`（绝不能删）**
   - 提供 ARMCLANG 半主机抑制：`__use_no_semihosting` / `__ARM_use_no_argv`
     + `FILE __stdout` + `_sys_exit` + `_ttywrch`。
   - 删掉后编译链接**可能照过**（无裸 printf 时符号被 `--gc-sections` 丢弃），
     但一旦出现 `printf`/`scanf`/`fopen` → 链入半主机 → 运行到 `_sys_open`/`_sys_write` 执行 **`BKPT`** 死机。
     **延迟暴露，极易误删。**
   - `bsp_log.c` **不能替代**它（bsp_log 的应用层 `_write()` 重定向被
     `#if defined(__GNUC__) && !defined(__ARMCC_VERSION)` 守卫挡住，MDK 侧走不到）。
   - 两者方向相反（一个必须排除、一个必须保留），是本项目最容易搞错的一对。

3. **HAL 库按需引入，禁止整包导入**
   - 反查：`grep -rhoE "\bHAL_[A-Za-z0-9_]+\(" app bsp Core | sed -E 's/HAL_([A-Za-z0-9]+)_.*/\1/' | sort -u`
   - 模块→文件特例：`UART`/`USART` → `stm32h7xx_hal_uart.c`（同一文件）；
     `RCC`/`FLASH`/`DMA`/`TIM`/`PWR` 必带 `_ex.c`。
   - 任何工程必备 4 基础件：`hal.c` `hal_cortex.c` `hal_rcc.c` `hal_rcc_ex.c`，再加 `hal_gpio.c`。
   - `stm32h7xx_hal_msp.c` / `stm32h7xx_it.c` 属用户 `Core/Src`，不算 HAL 模块。
   - 验收红线：HAL 入组 `.c` 应为个位数~十几个（非目录全量 ~80+）；CMake 与 Keil 文件数相等。
   - 引入新外设时必须两侧同步补，否则 `undefined reference to HAL_XXX_Init`。

## 日志方案：`bsp/bsp_log.c` 单文件范式

- 一份源码同时供 GCC 与 MDK 使用；`_write()` 重定向用
  `#if defined(__GNUC__) && !defined(__ARMCC_VERSION)` 守卫。
- `PRINT_LOG(fmt, ...)` 是**两参**形式（bsp_log）；旧的 `logger.h` 是四参
  `PRINT_LOG(LEVEL, tick, fmt, ...)` —— 迁移时需降参。
- API：`bsp_log_init()` / `bsp_log_write()` / `printf_log()` / `vprintf_log()` /
  `log_uart_tx_irq()`。TX 环形缓冲 1024 B。
- 删掉定义外设 handle 的文件（如 `drv_uart.c` 定义 `huart1`）时，
  **必须把 handle 定义搬到 `main.c`**，否则链接期才报 `undefined reference to huart1`。

## Keil 构建验证

- `UV4.exe -b -j0 -t <target> -x <uvprojx> -o build_log.htm`；exit 0 = 成功。
- 成败以 build_log.htm 末行 `0 Error(s), 0 Warning(s)` 为准（stdout 不一定回显）。
- 半主机验证：`fromelf --text -c Objects/*.axf | grep -c " BKPT"` 期望 0。
