/**
 * @file bsp_pin.h
 * @brief Board pin mux table for the RA8D1 Vision Board
 *
 * All pins are applied once here. R_IOPORT_Open() takes the whole table, so
 * every driver (UART / LED / GLCDC / SDRAM bus) must list its pins in this one
 * place instead of opening IOPORT a second time.
 */
#ifndef BSP_PIN_H_
#define BSP_PIN_H_

#include "bsp_api.h"

/** Apply the whole pin table. Called first thing in rt_hw_board_init(). */
fsp_err_t bsp_pin_init (void);

/** Runtime pin re-config (PFS rewrite on a single pin), e.g. to flip P1011
 *  from GPIO backlight into GPT peripheral mode when the camera starts. */
fsp_err_t bsp_pin_cfg (uint16_t pin, uint32_t pin_cfg);

/** Runtime GPIO level write (pins already configured as outputs). */
fsp_err_t bsp_pin_write (uint16_t pin, bsp_io_level_t level);

/** Runtime GPIO level read (pin must be in input mode to see the bus). */
fsp_err_t bsp_pin_read (uint16_t pin, bsp_io_level_t * p_level);

#endif /* BSP_PIN_H_ */
