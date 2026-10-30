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

/**
 * @brief  Re-read the boot sector / FAT of an already mounted volume.
 *
 *  Used after a USB mass-storage session: the host may have written to the
 *  card, so FatFs' cached FAT window and FSInfo are stale.  Open file objects
 *  survive (same FATFS instance), and the SDIO peripheral is left untouched, so
 *  this costs milliseconds instead of a full re-enumeration.
 *
 * @retval RT_OK / RT_FAIL   (RT_FAIL when no card is mounted)
 */
GLOBAL_TYPE_T sd_card_remount(void);

/**
 * @brief  Volume capacity and usage in MiB, for the transfer screen.
 *
 *  Reads the FSInfo/free-cluster count, so it must be called while the volume is
 *  mounted and NOT during a USB session.  Cost is one FAT walk on a dirty
 *  FSInfo, otherwise a cached read.
 *
 * @param  total_mb  total capacity in MiB, 0 when unknown
 * @param  used_mb   used space in MiB, 0 when unknown
 * @retval RT_OK / RT_FAIL
 */
GLOBAL_TYPE_T sd_card_usage_mb(uint32_t *total_mb, uint32_t *used_mb);

/** @retval 1 when the volume is mounted and usable. */
int sd_card_is_ready(void);

/** Capacity in MiB, or 0 when unknown. */
uint32_t sd_card_capacity_mb(void);

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H */
