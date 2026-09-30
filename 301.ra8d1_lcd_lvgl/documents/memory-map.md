# 内存映射（Phase 0）

> 主来源：官方工程 `memory_regions.ld` + `script/fsp.ld` + `board/board.h`。

## 1. 区域总表

| 区域 | 起始 | 长度 | 用途 | 来源 |
|---|---|---|---|---|
| ITCM | 0x00000000 | 64 KB | 关键代码/向量（可选） | [ld] |
| Code Flash | 0x02000000 | 0x1F8000 (~1.98MB) | 固件、向量表起始 | [ld] |
| Option Setting | 0x0300A100 | 0x100 | 芯片选项设置 | [ld] |
| DTCM | 0x20000000 | 64 KB | 关键数据（可选） | [ld] |
| SRAM | 0x22000000 | 0xE0000 (896 KB，实片 1MB) | .data/.bss/heap/stack | [ld]+[src] |
| Data Flash | 0x27000000 | 0x3000 | 常量/配置（可选） | [ld] |
| QSPI Flash | 0x60000000 | 1 MB（外挂） | XIP/存储（后续 Phase） | [ld]+[yaml] |
| SDRAM | 0x68000000 | 32 MB（区域上限 0x8000000） | framebuffer、大缓冲、memheap | [ld]+[cfg] |

## 2. Flash 内布局（本项目将遵循官方 fsp.ld 结构）

- 向量表：0x02000000 起始（Cortex-M85 取向量处，BSP `system.c`/启动文件负责）
- `.text/.rodata/.ARM.exidx` → FLASH；`.data` LMA 在 FLASH、VMA 在 SRAM（启动时搬运）
- BSP 保留 FLASH 顶部 0x8000 未用（FLASH_LENGTH=0x1F8000，总 2MB）

## 3. SRAM 内布局（0x22000000，896KB 可用）

- `.data` / `.bss` / `.noinit` 依次排布
- RT-Thread 官方工程：heap = `__RAM_segment_used_end__` → `0x22000000+896KB`
- 本项目（裸机/Nano）：Phase 0.5 自行在链接脚本显式定义 `_estack`（MSP 顶）与 heap 区间，**不复用官方脚本前先读一遍** `script/fsp.ld`

## 4. SDRAM 内布局（0x68000000，32MB，官方约定）

| 用途 | 段/符号 | 说明 |
|---|---|---|
| Framebuffer ×2 | `.sdram` 段（NOLOAD，64B 对齐），`__SDRAM_Start/End` | rgb 工程 800×480×2B×2 ≈ 1.6MB |
| 非缓存 DMA 区 | `.nocache_sdram` 段（NOLOAD，32B 对齐），`__nocache_sdram_start/End` | 供 CEU 等主设备写、CPU 读 |
| 其余 | 连续剩余空间 | RT-Thread 注册为 memheap "sdram"；本项目 Phase 2 起可作 framebuffer/大池 |

## 5. Cache / DMA 一致性结论（Phase 2 执行）

- Cortex-M85 默认上下电行为与 MPU 区域配置在 Phase 0.5/2 用寄存器读数确认，不凭记忆断言。
- 约定：**framebuffer 与 DMA 缓冲要么放 `.nocache_sdram`（MPU 非缓存），要么严格成对 clean/invalidate 并写明理由**。官方 BSP 用链接段 + MPU 的方式已提供基础设施。
