/**
 ******************************************************************************
 * @file    app_ui.h
 * @brief   Trackpad screen: touch surface (USB HID mouse) + 3 glass buttons.
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
 *         Widgets work with or without the SD card; the CJK glyphs simply stay
 *         absent when the card is missing.
 */
void app_ui_create(void);

/**
 * @brief  Called from the UI task every ~5 ms: samples the trackpad, drains the
 *         gesture queue, expires the "tap" pulses and refreshes the readouts.
 */
void app_ui_poll(void);

/**
 * @brief  Execute one serial-console command (USART1, one char per command).
 *
 *         Lets the whole HID path be exercised from a terminal, without
 *         touching the panel:
 *           l / m / r = tap left / middle / right button (150 ms)
 *           c = clear the trail          s / ? = status      h = help
 *
 *         Must be called from the UI task: it performs LVGL calls.
 */
void app_ui_command(char c);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_H__ */
