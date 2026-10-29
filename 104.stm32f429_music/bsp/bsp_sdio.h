/**
  ******************************************************************************
  * @file    bsp_sdio.h
  * @brief   microSD card driver over SDIO (4-bit wide bus), polled (no DMA).
  *
  *          Ported from embedded_based_on_stm32/code/00-Drivers/drv_sdio.c and
  *          adapted to this project's conventions (GlobalType_t instead of the
  *          foreign includes.h, no DMA, no ISR).
  *
  *  Pin map (STM32F429, AF12)
  *  ------------------------
  *    PC8  -> SDIO_D0
  *    PC9  -> SDIO_D1
  *    PC10 -> SDIO_D2
  *    PC11 -> SDIO_D3
  *    PC12 -> SDIO_CK
  *    PD2  -> SDIO_CMD
  *
  *  These pins are free on this board: the FMC (SDRAM + LCD) only claims
  *  PC0/PC2/PC3, PD0/PD1/PD8/PD9/PD10/PD14/PD15, PF*, PG* and PE*.
  *
  *  Clock
  *  -----
  *  SDIOCLK is the 48 MHz PLLQ output (the same one that feeds USB).  With
  *  ClockBypass disabled the card clock is SDIOCLK / (ClockDiv + 2):
  *      ClockDiv = 2  ->  48 MHz / 4 = 12 MHz   (conservative, very stable)
  *      ClockDiv = 0  ->  48 MHz / 2 = 24 MHz   (max rated for SDIO on F4)
  *  The card is enumerated in 1-bit mode and then switched to 4-bit.
  *
  *  Why polled and not DMA
  *  ----------------------
  *  DMA2 is shared with other peripherals on this board; the SD card is only
  *  used to pull glyphs off the font files (one sector at a time), so the
  *  polled path costs nothing that matters and removes an entire class of
  *  cache/DMA/IRQ conflicts.  It also keeps the driver callable from a
  *  FreeRTOS task without any FromISR plumbing.
  ******************************************************************************
  */
#ifndef BSP_SDIO_H
#define BSP_SDIO_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* Shared result type (also defined in bsp_lcd.h / bsp_lcd_text.h, guarded). */
#ifndef GLOBAL_TYPE_T_DEFINED
#define GLOBAL_TYPE_T_DEFINED
typedef enum
{
    RT_OK = 0,
    RT_FAIL,
} GlobalType_t;
#endif

#define SDIO_BLOCK_SIZE          512u
#define SDIO_RW_TIMEOUT_MS       1000u

/* SDIOCLK = 48 MHz (PLLQ=7).  Card clock = 48 MHz / (ClockDiv + 2).
 * 2 -> 12 MHz: rock solid for the first bring-up and for long cables. */
#define SDIO_CLOCK_DIV           2u

/**
  * @brief  Bring up SDIO + GPIO and identify the card.
  *
  *  Idempotent: calling it again after a success is a no-op, calling it again
  *  after a failure retries the whole enumeration (so a card plugged in later
  *  is picked up without a reset).
  *
  * @retval RT_OK  card detected and switched to the 4-bit bus
  * @retval RT_FAIL no card / enumeration error
  */
GlobalType_t bsp_sdio_init(void);

/**
  * @brief  Has a card been enumerated successfully?
  * @retval 1 ready, 0 not ready
  */
int bsp_sdio_is_ready(void);

/**
  * @brief  De-initialise the peripheral (used before a retry).
  */
void bsp_sdio_deinit(void);

/**
  * @brief  Read sectors (polled).
  * @param  buf          destination, MUST be 4-byte aligned
  * @param  start_block  first sector index
  * @param  nblocks      number of 512-byte sectors
  */
HAL_StatusTypeDef bsp_sdio_read_blocks(uint8_t *buf, uint32_t start_block, uint32_t nblocks);

/**
  * @brief  Write sectors (polled).
  * @param  buf          source, MUST be 4-byte aligned
  * @param  start_block  first sector index
  * @param  nblocks      number of 512-byte sectors
  */
HAL_StatusTypeDef bsp_sdio_write_blocks(const uint8_t *buf, uint32_t start_block, uint32_t nblocks);

/**
  * @brief  Card geometry, as reported by HAL_SD_GetCardInfo().
  * @retval RT_OK / RT_FAIL
  */
GlobalType_t bsp_sdio_get_info(uint32_t *block_count, uint16_t *block_size);

/* The HAL handle is exposed so the FatFs diskio glue can query card state. */
extern SD_HandleTypeDef hsd_card;

#endif /* BSP_SDIO_H */
