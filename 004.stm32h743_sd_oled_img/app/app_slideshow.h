/**
  ******************************************************************************
  * @file    app_slideshow.h
  * @brief   Cycles through the JPEGs found in 0:/image, one every 5 seconds.
  ******************************************************************************
  */

#ifndef __APP_SLIDESHOW_H
#define __APP_SLIDESHOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/** Directory scanned for pictures. */
#define SLIDESHOW_DIR           "0:/image"

/** Frame interval in milliseconds. */
#define SLIDESHOW_PERIOD_MS     5000U

/** Maximum number of pictures remembered per scan. */
#define SLIDESHOW_MAX_FILES     64

/** Longest file name kept (longer names are skipped with a warning). */
#define SLIDESHOW_MAX_NAME      64

/**
  * @brief  Scan SLIDESHOW_DIR and show the first picture straight away.
  * @note   Safe to call even when the card failed to mount: the poll routine
  *         will keep retrying in the background.
  */
void app_slideshow_init(void);

/**
  * @brief  Non-blocking tick, call it from the main loop as often as you like.
  *
  * Advances to the next picture once SLIDESHOW_PERIOD_MS has elapsed since the
  * previous frame was pushed to the panel.
  */
void app_slideshow_poll(void);

/** Number of pictures found during the last scan. */
uint32_t app_slideshow_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SLIDESHOW_H */
