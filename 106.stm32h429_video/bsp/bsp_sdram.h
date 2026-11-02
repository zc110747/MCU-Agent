/**
  ******************************************************************************
  * @file    bsp_sdram.h
  * @brief   External SDRAM (W9825G6KH-6, 32 MB) via FMC Bank1.
  *
  *          Memory map:
  *            SDRAM_BASE 0xC0000000  (FMC Bank1, 16-bit, 13 rows x 9 cols
  *                                    x 4 banks = 256 Mbit = 32 MB)
  *
  *          The LwIP memory pool and the FreeRTOS heap (ucHeap) live in SDRAM
  *          (see linker script sections .lwip_memp_pool / .freertos_heap).
  *          bsp_sdram_init() MUST run before any code touches 0xC0000000.
  ******************************************************************************
  */
#ifndef BSP_SDRAM_H
#define BSP_SDRAM_H

#include <stdint.h>

#define SDRAM_BASE                ((uint32_t)0xC0000000u)
#define SDRAM_SIZE                (32u * 1024u * 1024u)

/* SDRAM mode register definitions (W9825G6KH, programmed via LOAD_MODE) */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

/**
  * @brief  Initialize FMC + SDRAM controller and run a memory self-test.
  * @retval 0 on success, -1 on failure.
  */
int bsp_sdram_init(void);

#endif /* BSP_SDRAM_H */
