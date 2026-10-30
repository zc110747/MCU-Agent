/**
 ******************************************************************************
 * @file    app_ui.h
 * @brief   Touchpad demo screen: drawing area + gesture monitor.
 ******************************************************************************
 */
#ifndef __APP_UI_H__
#define __APP_UI_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Splash text shown while the SD card / fonts are being brought up.
 *         ASCII only: it is rendered before the GBK glyph files exist.
 */
void app_ui_boot(const char *ascii_msg);

/**
 * @brief  Build the main screen (call once the storage probe is done).
 *         Widgets work with or without the SD card; the wallpaper and the CJK
 *         glyphs simply stay absent when the card is missing.
 */
void app_ui_create(void);

/**
 * @brief  Called from the UI task every ~5 ms: drains the gesture queue, polls
 *         the keys and refreshes the counters / FPS readout.
 */
void app_ui_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_H__ */
