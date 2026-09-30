/**
 * @file touch_driver.h
 * @brief GT911 capacitive touch (Phase 3).
 *
 * The controller sits on the shared I2C bus at 0x5D, but its reset line is NOT
 * a GPIO: it is CH422G EXIO1, so the usual "let the driver toggle RST" flow
 * does not apply and the power-on/reset sequencing is done here by hand.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring the touch controller up and verify it answers.
 *
 * Requires i2c_bus_init() and io_expander_init() to have run.  Returns an
 * error if the controller does not respond, so a detached panel is reported at
 * boot instead of showing as "every tap is ignored".
 */
esp_err_t touch_init(void);

/** @brief Driver handle, or NULL when touch_init() has not succeeded. */
esp_lcd_touch_handle_t touch_handle(void);

/**
 * @brief Register the touch controller as an LVGL pointer input device.
 *
 * @param disp display to attach to (LVGL display 0)
 * @return the LVGL input device, or NULL on failure
 */
lv_indev_t *touch_attach_lvgl(lv_display_t *disp);

#ifdef __cplusplus
}
#endif
