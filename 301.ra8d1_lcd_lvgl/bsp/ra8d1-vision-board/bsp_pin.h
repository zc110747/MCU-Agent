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

#endif /* BSP_PIN_H_ */
