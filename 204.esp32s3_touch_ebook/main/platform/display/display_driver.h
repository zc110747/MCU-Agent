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

/**
 * @brief Read back what is actually in the panel frame buffers.
 *
 * The console can prove the software chain came up, but it cannot prove a
 * single pixel reached the glass.  This closes that gap from the other end:
 * LVGL runs in direct mode, so the panel's own frame buffers *are* the draw
 * buffers, and whatever LVGL painted is sitting right there in memory.  A
 * census of those buffers therefore answers two questions that otherwise need
 * eyes on the screen:
 *
 *   - did anything ever get drawn (non_zero > 0)?
 *   - is the byte order right (corner/centre compare against the theme colour)?
 *
 * Cheap enough to call from a diagnostic page, but it walks the whole frame,
 * so do not put it on a per-frame path.
 */
typedef struct {
    bool     present;        /* buffer pointer was returned by the driver   */
    uint32_t sampled;        /* pixels inspected (0 when absent)            */
    uint32_t non_zero;       /* pixels with a non-zero value                */
    uint16_t corner[4];      /* (0,0) (1,0) (0,1) (1,1)                     */
    uint16_t centre;         /* pixel at (w/2, h/2)                         */
} display_fb_census_t;

typedef struct {
    display_fb_census_t fb[BOARD_LCD_NUM_FB];
    uint16_t bg_rgb565;      /* theme background, for comparison            */
    uint16_t surface_rgb565; /* theme surface, for comparison               */
} display_fb_report_t;

/** @brief Fill `out` with a census of every panel frame buffer. */
esp_err_t display_fb_report(display_fb_report_t *out);

#ifdef __cplusplus
}
#endif
