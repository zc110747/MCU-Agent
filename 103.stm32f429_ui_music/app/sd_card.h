/**
  ******************************************************************************
  * @file    sd_card.h
  * @brief   microSD card bring-up: SDIO peripheral + FatFs volume "1:".
  *
  *  This is the "first choice" source for the GBK font files.  The boot
  *  loader (app/ui_task.c) calls sd_card_init() once the LVGL boot screen is
  *  up; if it fails the loader falls back to the USB mass-storage volume "0:".
  *
  *  sd_card_init() is intentionally NOT latched on failure: a card inserted
  *  later is picked up on the next call, which is what makes hot-plug work
  *  without a reset.
  ******************************************************************************
  */
#ifndef SD_CARD_H
#define SD_CARD_H

#include "bsp_sdio.h"

/**
  * @brief  Enumerate the SD card over SDIO and mount FatFs volume "1:".
  *         Safe to call repeatedly; a no-op when already mounted.
  *
  * @retval RT_OK   card detected, 4-bit bus up, filesystem mounted
  * @retval RT_FAIL no card / SDIO error / filesystem not readable
  */
GlobalType_t sd_card_init(void);

/**
  * @brief  Forget the current card (unmount + drop the SDIO state) so the
  *         next sd_card_init() re-enumerates from scratch.
  */
void sd_card_invalidate(void);

/**
  * @brief  Suppress / restore the "no card" debug line.
  *
  *  The loader re-probes the socket periodically so a card inserted later is
  *  still picked up.  Without this the console would be flooded with one
  *  "SDIO init FAILED" per retry.
  */
void sd_card_set_quiet(int on);

/**
  * @brief  Is the card mounted and usable?
  * @retval 1 yes, 0 no
  */
int sd_card_is_ready(void);

/**
  * @brief  Card capacity in MiB, or 0 when unknown.
  */
uint32_t sd_card_capacity_mb(void);

#endif /* SD_CARD_H */
