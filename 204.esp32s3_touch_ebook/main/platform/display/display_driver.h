/**
 * @file display_driver.h
 * @brief RGB LCD platform driver (init, frame buffers, backlight).
 *
 * Owns the esp_lcd RGB panel. Nothing above the platform layer may touch the
 * LCD GPIOs or the CH422G directly.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Reset and start the RGB panel. Must run after io_expander_init(). */
esp_err_t display_init(void);

/** Underlying esp_lcd panel handle (needed by the LVGL display adapter). */
esp_lcd_panel_handle_t display_panel(void);

/** Panel geometry in the native landscape orientation. */
static inline int display_width(void)  { return BOARD_LCD_H_RES; }
static inline int display_height(void) { return BOARD_LCD_V_RES; }

/**
 * @brief Backlight on/off.
 *
 * The 4.3" panel's LED string is driven by an AP3032 boost converter whose
 * CTRL pin is wired to CH422G EXIO2 ("DISP").  There is no PWM / current
 * control line on this board, so the hardware supports on/off only.
 */
esp_err_t display_backlight_set(bool on);

/** Current backlight state. */
bool display_backlight_get(void);

#ifdef __cplusplus
}
#endif
