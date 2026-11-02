/**
 * @file    bsp_sdio.h
 * @brief   microSD card driver over SDIO (4-bit wide bus): DMA reads, polled writes.
 * @note    document/stm32f429/video_player_port.md A5
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
} GlobalType_t;
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
GlobalType_t bsp_sdio_init(void);

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
GlobalType_t bsp_sdio_get_info(uint32_t *block_count, uint16_t *block_size);

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
