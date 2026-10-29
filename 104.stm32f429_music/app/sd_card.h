/**
 ******************************************************************************
 * @file    sd_card.h
 * @brief   microSD bring-up: SDIO peripheral + FatFs volume "0:".
 ******************************************************************************
 */
#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>

#include "bsp_sdio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Enumerate the card over SDIO and mount FatFs "0:".
 *         Safe to call repeatedly; a no-op when already mounted.
 * @retval RT_OK   card detected, 4-bit bus up, filesystem mounted
 * @retval RT_FAIL no card / SDIO error / unreadable filesystem
 */
GLOBAL_TYPE_T sd_card_init(void);

/** Unmount and drop the SDIO state so the next init re-enumerates. */
void sd_card_invalidate(void);

/** @retval 1 when the volume is mounted and usable. */
int sd_card_is_ready(void);

/** Capacity in MiB, or 0 when unknown. */
uint32_t sd_card_capacity_mb(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H */
