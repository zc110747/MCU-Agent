# 链接脚本（.ld）内存边界必须实测

- 栈顶 `_estack` 与 `RAM LENGTH` 必须与芯片**真实容量**一致——ST 官方模板常带错尺寸。
- **STM32F429IG 易错**：连续 SRAM 仅 192K（`0x20000000`~`0x2002FFFF`），CCM 64K @`0x10000000`
  不连续、**ETH/DMA 访问不到**。ST 模板常误写 256K/2048K → 内存越界 HardFault。
- **外部内存（SDRAM）段必须 `(NOLOAD)`**：startup 的 bss 清零在 FMC 初始化**之前**执行，
  SDRAM 段若参与清零会 HardFault。
- 改完 `.ld` 必须让 CMake 跟踪（`LINK_DEPENDS`），否则 ninja 报 `no work to do`、
  实际用的还是旧布局。
