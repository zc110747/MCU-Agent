/**
  ******************************************************************************
  * @file    bsp/mpu.c
  * @brief   MPU configuration for the STM32H743 bootloader.
  *
  * Requirement: during a firmware upgrade the internal flash is erased and
  * reprogrammed from this very bootloader. To guarantee the CPU never reads
  * stale cache lines (and never buffers write data that would bypass the flash
  * controller), every region this firmware touches is configured as Normal
  * NON-CACHEABLE:
  *
  *   Region 0 : 0x08000000, 2 MB  internal flash (execute + program + verify)
  *   Region 1 : 0x20000000, 128 KB DTCM         (stack)
  *   Region 2 : 0x24000000, 512 KB AXI SRAM     (.data/.bss, USB buffers)
  *   Region 3 : 0x90000000, 8 MB  QSPI flash    (future XIP / safety)
  *
  * D-Cache and I-Cache stay disabled in the bootloader: with all regions
  * non-cacheable they add nothing, and disabling them removes every
  * cache-coherency hazard for flash writes and USB DMA.
  ******************************************************************************
  */
#include "mpu.h"

/* ARMv7-M MPU RASR field helpers (bit positions per ARMv7-M ARM). */
#define MPU_RASR_XN_Pos   28U
#define MPU_RASR_AP_Pos   24U
#define MPU_RASR_TEX_Pos  19U
#define MPU_RASR_S_Pos    18U
#define MPU_RASR_C_Pos    17U
#define MPU_RASR_B_Pos    16U
#define MPU_RASR_SIZE_Pos 1U
#define MPU_RASR_EN_Pos   0U

/* AP = full read/write access at both privilege levels (0b011). */
#define MPU_AP_FULL_ACCESS  (0x3UL << MPU_RASR_AP_Pos)

static void mpu_region(uint32_t rnr, uint32_t base, uint32_t size_field)
{
    MPU->RNR  = rnr;
    MPU->RBAR = base | (1UL << 4) | rnr;   /* VALID | region number */
    /* Normal, Non-Cacheable: TEX=0b001, S=0, C=0, B=0, XN=0 */
    MPU->RASR = (1UL << MPU_RASR_TEX_Pos)
              | MPU_AP_FULL_ACCESS
              | (size_field << MPU_RASR_SIZE_Pos)
              | (1UL << MPU_RASR_EN_Pos);
    __DSB();
}

void BSP_MPU_Init(void)
{
    /* Disable MPU before reconfiguring. */
    MPU->CTRL = 0U;
    __DSB();
    __ISB();

    /* Region 0: internal flash, 2 MB (SIZE field = log2(2MB) - 1 = 20) */
    mpu_region(0U, 0x08000000UL, 20U);
    /* Region 1: DTCM, 128 KB (SIZE = log2(128K) - 1 = 16) */
    mpu_region(1U, 0x20000000UL, 16U);
    /* Region 2: AXI SRAM, 512 KB (SIZE = log2(512K) - 1 = 18) */
    mpu_region(2U, 0x24000000UL, 18U);
    /* Region 3: QSPI memory-mapped window, 8 MB (SIZE = log2(8M) - 1 = 22) */
    mpu_region(3U, 0x90000000UL, 22U);

    /* Enable MPU; privileged code still uses the default memory map for
       addresses not covered by a region (PRIVDEFENA). */
    MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
    __DSB();
    __ISB();
}
