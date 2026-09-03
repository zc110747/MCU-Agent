---
name: soc-cache-mpu
description: STM32（尤其 H7）缓存与 MPU 的架构正确用法：D-Cache 应保持开启，即便外设上 DMA 也绝不全局关 D-Cache，而是用 MPU 把 DMA 缓冲区标记为非缓存（non-cacheable）区；SDIO 非 DMA（CPU 搬 FIFO）时开 D-Cache 完全无一致性问题。含 H7 内存/总线域要点（IDMA 不可达 DTCM、SPI6 在 D3 域用 BDMA 缓冲放 SRAM4）、MPU region 规划草案、以及如何代码反查"是否真用了 DMA"避免被过时注释误导。适用于"D-Cache 要不要关""DMA 缓冲怎么放""MPU 怎么配""SD/SDIO 一致性""SPI DMA 缓冲选址"等问题。触发词：DCache、MPU、non-cacheable、缓存一致性、DMA 缓冲、IDMA、BDMA、SRAM4、DTCM、关闭缓存、AXI-SRAM。
agent_created: true
---

# STM32 缓存与 MPU：用 MPU 而非关 D-Cache

## 一、核心原则（用户铁律，2026-09-03 明确）

> **D-Cache 应该保持开启。即便外设上了 DMA，也不该全局关 D-Cache；
> 正确做法是让 MPU 把 DMA 缓冲区单独标记为非缓存区。**

- SDIO **没用 DMA**（CPU 走 SDIO 内部 FIFO 搬数据）时，开 D-Cache 完全无一致性问题。
- 上 DMA 时，**只**把 DMA 共享的那块 RAM 划成 non-cacheable，其余 RAM 仍 cacheable 享受加速。
- 全局 `SCB_DisableDCache()` 是偷懒且错误的做法——它牺牲了整个系统的内存带宽。

## 二、H7 内存 / 总线域要点（规划前提）

| 区域 | 地址 | 域 | 备注 |
|------|------|----|------|
| DTCM | 0x20000000 | D1(紧耦合) | **IDMA / BDMA 不可达**；只能 CPU 直访，适合低延迟数据 |
| AXI-SRAM | 0x24000000 | D1 | 512 KB，IDMA/BDMA 均可达；**通用 RAM / 栈 / 字体池首选** |
| SRAM_D2 | 0x30000000 | D2 | 288 KB，IDMA 可达 |
| SRAM4 (D3) | 0x38000000 | D3 | 64 KB，**SPI6 在 D3 域**，同域 BDMA 最顺、零总线争用 |

- **SDMMC1 内部 DMA（IDMA）**：可达 AXI-SRAM / D2 / D3，**不可达 DTCM** → SD 扇区缓冲放
  SRAM_D2 或 AXI-SRAM 合法，放 DTCM 会 DMA 出错。
- **SPI6 显示（D3 域）**：DMA 走 **BDMA(D3)**，像素缓冲放 **SRAM4(0x38000000)** 同域最顺。
  > 整帧 240×240×2 = 115 KB 放不下 SRAM4(64K)，SPI6 DMA 缓冲只能做**行/块流式发送**
  >（如 1~4 KB 行缓冲），而非整帧帧缓冲。

## 三、MPU Region 规划草案（SDMMC IDMA + SPI6 DMA 场景）

| # | Base | Size | 属性 | 用途 | Bank |
|---|------|------|------|------|------|
| 0 | 0x24000000 | 512 KB | Normal **Cacheable**(WT/WB) | 通用 RAM / 栈 / 字体池 | AXI-SRAM(D1) 现状保留 |
| 1 | 0x30000000 | 16 KB | Normal **Non-cacheable** | SDMMC IDMA 扇区缓冲 | SRAM_D2(D2) |
| 2 | 0x38000000 | 16 KB | Normal **Non-cacheable** | SPI6(BDMA) LCD 发送缓冲 | SRAM4(D3) |
| 3 | 余下 SRAM_D2 | 余下 | Cacheable | D2 其他数据（可选） | SRAM_D2 |

**关键点**：
- Region1/2 标 **non-cacheable** → CPU 与 DMA 直接看同一份内存，**无需手动
  `SCB_CleanDCache` / `SCB_InvalidateDCache`**，这是选 non-cacheable 的**核心收益**。
- Region1/2 与 Region0 **不同 bank、不重叠**，MPU region 优先级无关。
- 配套改动：链接脚本增 `.sd_idma_buf`(>RAM_D2) / `.spi6_dma_buf`(>RAM_D3) 两段 NOLOAD；
  C 端 `__attribute__((section(".sd_idma_buf"), aligned(16)))` 定义；`MPU_Config()` 扩
  Region1/2（MAIR=non-cacheable）；SD 改 `HAL_SD_ReadBlocks_DMA` + 完成回调、SPI6 改
  `HAL_SPI_Transmit_DMA`。

## 四、如何验证"是否真用了 DMA"（避免被过时注释坑）

代码注释会和真实实现矛盾。**别信注释，反查代码**：

1. SDIO 读路径：`HAL_SD_ReadBlocks()` = **轮询**（CPU 搬 FIFO，非 DMA）；
   `HAL_SD_ReadBlocks_DMA()` = **IDMA**。搜工程确认实际调用的是哪个。
2. SPI 发送：`HAL_SPI_Transmit()` = 阻塞/轮询；`HAL_SPI_Transmit_DMA()` = DMA。
3. 缓存状态：`SCB_EnableDCache()` 是否调用；MPU Region0 是否把 AXI-SRAM 标 cacheable。
4. **结论自洽检查**：若代码是"非 DMA + D-Cache 开" → 无一致性问题，现状正确，不要去关缓存。

> 实测案例（003 工程）：`main.c:48` 实际 `SCB_EnableDCache()`（D-Cache 开着），
> `drv_sdio.c` 用 `HAL_SD_ReadBlocks()` 轮询（非 DMA），注释却写"D-Cache 关 / SD 用内部 DMA"——
> 注释是**过时错误**，已订正为符合现状的文字。真实状态 = 用户原则的正确体现。

## 五、何时真正需要落地 MPU non-cacheable 区

- 当前 SDIO 轮询 + D-Cache 开：**无需改动，方案可用**。
- 仅当把 SDIO 切 `ReadBlocks_DMA` 或 SPI6 切 `Transmit_DMA` 时，才需要按第三节规划
  划 non-cacheable 缓冲区。**规划由用户配合定稿**（缓冲大小、SRAM 选址、是否现在落地）。
