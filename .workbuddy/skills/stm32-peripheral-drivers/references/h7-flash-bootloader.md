# STM32H7 内部 Flash 升级引擎（Bootloader 实战坑）

H7 内部 Flash 是**双 Bank（16×128KB）**，做 Bootloader 升级时，下面每一条都能让"擦写函数一跑就死"：

## 1. DTCM 不可执行（最高频致命坑）
Cortex-M7 的 **I-Code 总线无法从 DTCM(0x20000000) 取指**。把"从 RAM 执行"的擦写引擎放进 DTCM → 一调用就 BusFault → `Default_Handler`(Infinite_Loop)。
**引擎必须放 AXI SRAM(0x24000000)** 或 SRAM1-4（可执行且非 bank1）。MPU 的 XN=0 管不到 TCM 硬件约束 —— 改 MPU 没用。

## 9.2 双 Bank 擦写取指
擦/写 bank1 时 CPU 不能从 bank1 取指。擦写引擎须整体在独立可执行 RAM 内，且调用链（含所有 static helper，如 `addr_to_bank_sector`）都得带 RAM 段属性，不能残留 bank1 Flash 调用。

## 9.3 RWW 只 stall 不 fault
H7 的 read-while-write 会让总线停滞但**不 HardFault**。所以从 bank1 取指编程 bank1 不会进 `Infinite_Loop` —— 这正好解释为什么参考代码能直接调用位于 bank1 的 `HAL_FLASH_Program`。

## 9.4 手搓寄存器不可靠 → 用 HAL
自写 `FLASH->CR` 序列极易踩 PSIZE/时序细节。改用**经验证 HAL 原语**：
- 擦除：`HAL_FLASHEx_Erase()`（`VoltageRange = FLASH_VOLTAGE_RANGE_3`）
- 编程：`HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr, (uint32_t)src)`（一次写 256 位 = 8×32bit）
- **每次操作前清双 Bank 错误标志**：`__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_ALL_ERRORS_BANK2)`
- 用 `__disable_irq()`+`HAL_FLASH_Unlock()` 包住、`HAL_FLASH_Lock()`+`__enable_irq()` 收尾。

## 5. 两阶段长度擦除
不要整片擦 1..14。先 `BFLASH_EraseApp(len)` 按 App 长度只擦实际占用（不含末扇区），末扇区由 `BFLASH_EraseAppLastSector(len)` 在**编程前即时擦**（避免整片先擦 + 中途掉电变砖）。

## 6. 跳转 App 序列（缺一不可）
跳前必须：`HAL_DeInit` / 关全局中断 / 关 MPU / 关 I-D Cache / 设 MSP / 重定位 VTOR / `__enable_irq`（清 PRIMASK 残留）。
- **App 工程必须提供 `SysTick_Handler`** 且 `main()` 开头 `__enable_irq()`，否则 `HAL_Delay` 卡死（缺 handler → 链到 `Default_Handler` 死循环；bootloader 留 PRIMASK=1 未恢复 → 全局中断关死）。
- 验证链路（gdb 直调编程函数、Flash 回读）见 `stm32-verification-acceptance`。
- **擦写引擎放 AXI SRAM(0x24000000)**，绝不放 DTCM（见第 1 节）。
