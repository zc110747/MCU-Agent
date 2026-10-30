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
GlobalType_t sd_card_init(void);

/** Unmount and drop the SDIO state so the next init re-enumerates. */
void sd_card_invalidate(void);

/**
 * @brief  Volume capacity and usage in MiB, for the status bar.
 *
 *  Uses the cached free-cluster count when the FSInfo is clean; only walks the
 *  FAT when it is small enough to be quick.  Must be called with the volume
 *  mounted.
 *
 * @param  total_mb  total capacity in MiB, 0 when unknown
 * @param  used_mb   used space in MiB, 0 when unknown
 * @retval RT_OK / RT_FAIL
 */
GlobalType_t sd_card_usage_mb(uint32_t *total_mb, uint32_t *used_mb);

/** @retval 1 when the volume is mounted and usable. */
int sd_card_is_ready(void);

/** Capacity in MiB, or 0 when unknown. */
uint32_t sd_card_capacity_mb(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H */
