/**
 * @file io_expander.h
 * @brief CH422G I2C I/O expander (U11) -- owns LCD_RST / DISP / CTP_RST / SD_CS.
 *
 * The four control signals that the ESP32-S3 has no free GPIO for are routed
 * through this chip.  Nothing above the platform layer is allowed to touch it.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "board_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create the bus, register the CH422G and put every output in a safe state. */
esp_err_t io_expander_init(void);

/** Drive one EXIO line (BOARD_EXIO_*) to the given logic level. */
esp_err_t io_expander_set_level(board_exio_t pin, bool level);

/** Current logic level of one EXIO line (mirrors the internal shadow register). */
bool io_expander_get_level(board_exio_t pin);

/** Last byte written to the CH422G output register. */
uint8_t io_expander_get_value(void);

/** Write the whole output register at once (bit N == EXION). */
esp_err_t io_expander_write(uint8_t value);

/** Convenience wrappers for the named board signals. */
esp_err_t io_expander_lcd_reset(bool level);      /* EXIO3, active low */
esp_err_t io_expander_touch_reset(bool level);    /* EXIO1, active low */
esp_err_t io_expander_backlight_enable(bool on);  /* EXIO2, high = on  */
esp_err_t io_expander_sd_cs(bool level);          /* EXIO4, active low */

#ifdef __cplusplus
}
#endif
