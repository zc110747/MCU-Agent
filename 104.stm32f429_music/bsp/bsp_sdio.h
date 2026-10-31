/**
 ******************************************************************************
 * @file    bsp_sdio.h
 * @brief   microSD card driver over SDIO (4-bit wide bus): DMA reads, polled
 *          writes.
 *
 *          Ported from embedded_based_on_stm32/code/00-Drivers/drv_sdio.c and
 *          adapted to this project's conventions (GLOBAL_TYPE_T instead of the
 *          foreign includes.h).
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
 *  DMA
 *  ---
 *  Reads use DMA2 Stream6 / Channel 4 (the SDIO request line on the F429;
 *  AN4031 Table 2 lists SDIO on both Stream3 Ch4 and Stream6 Ch4).  The audio
 *  path keeps DMA2 Stream3, so the two never have to be reconfigured for each
 *  other.  Writes stay polled -- they only come from USB MSC write10 and the
 *  TX FIFO has no overrun risk.
 *
 *  A short DMA burst is not an error: when the card ends the data path with a
 *  few words still queued in the SDIO FIFO, the driver drains them by hand and
 *  closes the transfer with HAL_SD_Abort().  Real errors are retried twice
 *  (SDIO_IO_RETRIES), each attempt preceded by HAL_SD_Abort().
 ******************************************************************************
 */
#ifndef BSP_SDIO_H
#define BSP_SDIO_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared result type (also defined in bsp_lcd.h / bsp_lcd_text.h, guarded). */
#ifndef GLOBAL_TYPE_T_DEFINED
#define GLOBAL_TYPE_T_DEFINED
typedef enum {
    RT_OK = 0,
    RT_FAIL,
} GLOBAL_TYPE_T;
#endif

#define SDIO_BLOCK_SIZE 512u
#define SDIO_RW_TIMEOUT_MS 1000u

/* SDIOCLK = 48 MHz (PLLQ=7).  Card clock = 48 MHz / (ClockDiv + 2).
 * 2 -> 12 MHz: rock solid for the first bring-up and for long cables. */
#define SDIO_CLOCK_DIV 2u

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
GLOBAL_TYPE_T bsp_sdio_init(void);

/**
 * @brief  Has a card been enumerated successfully?
 * @retval 1 ready, 0 not ready
 */
uint8_t bsp_sdio_is_ready(void);

/**
 * @brief  De-initialise the peripheral (used before a retry).
 */
void bsp_sdio_deinit(void);

/**
 * @brief  Read sectors (DMA, aborted and replayed on error).
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
GLOBAL_TYPE_T bsp_sdio_get_info(uint32_t *block_count, uint16_t *block_size);

/**
 * @brief  Diagnostic counters, all optional (pass NULL to skip).
 * @param  tail_fixes  bursts whose FIFO tail had to be recovered by hand
 * @param  dma_errors  bursts that failed outright
 * @param  retry_logs  how many failure lines were printed
 */
void bsp_sdio_stats(uint32_t *tail_fixes, uint32_t *dma_errors, uint32_t *retry_logs);

/* IRQ entry points, called from app/stm32f4xx_it.c. */
void bsp_sdio_dma_rx_irq_handler(void);
void bsp_sdio_irq_handler(void);

/* The HAL handle is exposed so the FatFs diskio glue can query card state. */
extern SD_HandleTypeDef hsd_card;

#ifdef __cplusplus
}
#endif

#endif /* BSP_SDIO_H */
