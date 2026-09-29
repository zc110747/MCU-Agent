# MEMORY.md — STM32H743 Bootloader 项目长期笔记

## ⚠️ 关键约束（H7 通用坑）
- **Cortex-M7 DTCM(0x20000000) 不可执行代码**：I-Code 总线取不到 DTCM 指令，从 DTCM 跑函数会立即 BusFault→Default_Handler。凡"从 RAM 执行"的引擎（flash 擦写、bootloader 跳转引擎等）**绝不放 DTCM**，必须放 AXI SRAM(0x24000000)/SRAM1-4（可执行且非 bank1）。MPU 的 XN=0 管不到 TCM 硬件约束。
- **双 Bank Flash 擦写取指**：擦/写 bank1 时 CPU 不能从 bank1 取指；擦写引擎须整体在独立可执行 RAM 内，且调用链（含所有 static helper，如 addr_to_bank_sector）都得带 RAM 段属性，不能残留 bank1 Flash 调用。
- openocd 的 `stmqspi` 在本沙箱无法拉起 H743 QSPI（probe 后 timeout 或 No QSPI），且无 mtools；升级包走设计的 U 盘(OTG_FS)路径，沙箱只有 ST-Link VCP(COM19)+SWD。

## 工程约定
- **双工具链共用同一份源码**：GCC 侧 `CMakeLists.txt`（权威源集清单）+ Keil 侧 `MDK-ARM/stm32h743.uvprojx`。**改共享源码（`bsp/flash_upgrade.c`、`bsp/usb_board.c`、`sys_startup/arm/startup_stm32h743xx.s`）必须跑一次 GCC 回归**比对 `text/data/bss`（当前基线 `72264/2244/39848`）——已验证逐字节一致才可提交。
- **Keil 构建**：`UV4.exe -b -j0 -t stm32h743 -x MDK-ARM\stm32h743.uvprojx -o MDK-ARM\build_log.htm`；成败看 `build_log.htm` 末行（不是退出码/stdout）。当前：**0 Error / 0 Warning**，Code 49844 / RO 4452 / RW 48 / ZI 39168。
  - Keil **无 `--gc-sections`**：GCC 下靠死代码摘除而"从未暴露"的未定义符号会在 Keil 爆 `L6218E`。已踩：`tusb_time_millis_api()`（`CFG_TUSB_OS=OPT_OS_NONE`，实现在 `bsp/usb_board.c`）。
  - **`.upgrade_ram` 的 load/run 分离**：armlink 不支持嵌套 load region，必须两个平级顶层 load region（`LR_UPGRADE 0x0801E000` 装载 → `UPGRADE_RAM 0x24000000` 运行）。区域符号名取自**执行域标签**：`Image$$UPGRADE_RAM$$Base/$$Limit`、`Load$$UPGRADE_RAM$$Base`。
  - 多 load region 下 After-Build **必须 `fromelf --bincombined`**（`--bin` 会产出目录）。
  - `bsp/syscalls.c` **必须从 Keil 排除**；`MDK-ARM/mdk_target.c`（`__use_no_semihosting`）**必须保留**——方向相反，删错一个是延迟暴露的 `BKPT` 停机。
  - 镜像里的 61 个 `BKPT` 是 TinyUSB `TU_BREAKPOINT()` 的 DHCSR 守卫（无害）；半主机 `SVC 0xAB` 计数为 **0**。
- 日志接口：应用代码统一用 `PRINT_LOG(fmt, ...)`（`bsp/bsp_log.h`），**不要**直接调 `BSP_UART_*`。非阻塞（256 B 栈缓冲 → 1 KB TX 环形缓冲 → TXE 中断排空）；`PRINT_LOG_ENABLE=0` 编译期全消除。
  - `bsp_log.c` **独占 USART1 句柄**——别处再声明 USART1 的 `UART_HandleTypeDef` 会让 TXE ISR 驱动另一份影子副本。
  - `USART1_IRQHandler(){ log_uart_tx_irq(); }` 必须存在于**每个** `stm32h7xx_it.c`（现在有 bootloader 与 test_app 两份）。缺了它环形缓冲填满后日志**静默停止且不报错**，是最容易漏的一步。
  - `printf_log()` 会把每个字节追加到 `bsp/uart.c` 的 `g_uart_log`（16 KB）；`capture.py` 依赖这个符号名和 `0x4000` 长度走 SWD dump，改名字/长度要同步改 `capture.py` 的 `LOG_SIZE`。
- `bsp/uart.c/h` 是兼容层（`BSP_UART_Init`→`bsp_log_init`、`SendStr/SendBuf`、`g_uart_log`），不是驱动。
- 升级包(产出)位置：`test_app/dist/`（`stm32h7_test.bin`+`verify.json`+`stm32h7_boot.bin`），由用户拷到 U 盘自测；沙箱不主动写 QSPI。
- 升级流程：U 盘(UPDATE.BIN+verify.json) → bootloader 解析(mjson) → HMAC-SHA256/版本校验 → 按 app 长度擦除(末块升级前擦) → 流式编程+读回校验 → 更新 config(CRC32 保护) → 卸载 → 复位跳转。
- bootloader 修复必须经 openocd 烧内部 Flash 才生效；仅换 U 盘 app 包无效。

## 无真机时的构建期取证手段（替代上板）
- **中断接线必须查向量表，不能只看符号存在**：解析 `.isr_vector` 确认目标 IRQ 槽指向 `<Handler>+1`（thumb）而非 `Default_Handler`。handler 名字写错编译照样过，运行时静默死循环。
  `arm-none-eabi-nm elf | grep Default_Handler` 取基准地址，再按 `vec[16+IRQn]` 比对。
- **验收串核对**：`objdump -s -j .rodata` 扫字符串，确认 `verify_serial.py` 等脚本 grep 的串仍在镜像里（改格式串极易失手）。
- **test_app 构建（README 4.2）**：`cd test_app && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`。必须排除根 `app/main.c`+`app/stm32h7xx_it.c`（否则 main/SysTick_Handler 重复定义），且要带 `bsp/syscalls.c`（否则 newlib 报 `_sbrk`/`_write` undefined）。
- **栈/堆两处存值**：GCC `.ld` 与 `arm/startup_stm32h743xx.s` 各有一份，现均对齐 `Stack 0x400` / `Heap 0x200`（实测 `__initial_sp = 0x20000400`）。改动任一处要同步另一处。

