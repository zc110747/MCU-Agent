/**
 * @file lvgl_port.h
 * @brief LVGL binding for this board.
 *
 * This is the *only* translation unit in the project that is allowed to talk
 * to esp_lvgl_port / lvgl internals.  Everything above the platform layer uses
 * these four functions, so the day we need to change the LVGL task priority,
 * switch off direct mode or add a second display, exactly one file changes.
 *
 * Naming note: the underlying component exports lvgl_port_lock()/unlock().
 * Our wrappers are deliberately called acquire()/release() so the two sets of
 * symbols can never collide at link time.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start LVGL: init the port, then attach the RGB panel as display 0.
 *
 * Must be called after display_init() (the panel handle has to exist) and
 * before the backlight is switched on, so the first visible frame is already
 * a defined one.
 */
esp_err_t lvgl_port_start(void);

/** @brief Take the LVGL mutex. 0 waits forever. */
bool lvgl_port_acquire(uint32_t timeout_ms);

/** @brief Give the LVGL mutex back. */
void lvgl_port_release(void);

/** @brief The LVGL display created for the panel, or NULL before start. */
lv_display_t *lvgl_port_display(void);

#ifdef __cplusplus
}
#endif
