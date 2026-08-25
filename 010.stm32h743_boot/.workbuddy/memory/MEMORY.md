# MEMORY.md — STM32H743 Bootloader 项目长期笔记

## ⚠️ 关键约束（H7 通用坑）
- **Cortex-M7 DTCM(0x20000000) 不可执行代码**：I-Code 总线取不到 DTCM 指令，从 DTCM 跑函数会立即 BusFault→Default_Handler。凡"从 RAM 执行"的引擎（flash 擦写、bootloader 跳转引擎等）**绝不放 DTCM**，必须放 AXI SRAM(0x24000000)/SRAM1-4（可执行且非 bank1）。MPU 的 XN=0 管不到 TCM 硬件约束。
- **双 Bank Flash 擦写取指**：擦/写 bank1 时 CPU 不能从 bank1 取指；擦写引擎须整体在独立可执行 RAM 内，且调用链（含所有 static helper，如 addr_to_bank_sector）都得带 RAM 段属性，不能残留 bank1 Flash 调用。
- openocd 的 `stmqspi` 在本沙箱无法拉起 H743 QSPI（probe 后 timeout 或 No QSPI），且无 mtools；升级包走设计的 U 盘(OTG_FS)路径，沙箱只有 ST-Link VCP(COM19)+SWD。

## 工程约定
- 升级包(产出)位置：`test_app/dist/`（`stm32h7_test.bin`+`verify.json`+`stm32h7_boot.bin`），由用户拷到 U 盘自测；沙箱不主动写 QSPI。
- 升级流程：U 盘(UPDATE.BIN+verify.json) → bootloader 解析(mjson) → HMAC-SHA256/版本校验 → 按 app 长度擦除(末块升级前擦) → 流式编程+读回校验 → 更新 config(CRC32 保护) → 卸载 → 复位跳转。
- bootloader 修复必须经 openocd 烧内部 Flash 才生效；仅换 U 盘 app 包无效。
