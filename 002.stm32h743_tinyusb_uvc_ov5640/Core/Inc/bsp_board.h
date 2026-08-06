/**
 ******************************************************************************
 * @file    bsp_board.h
 * @brief   Low level board services: clock tree, MPU, cache, LED, logging.
 ******************************************************************************
 */

#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Called from main() before HAL_Init(). Configures the MPU so the frame
 * buffer region in AXI SRAM is non-cacheable, then enables I/D cache. */
void bsp_mpu_config(void);
void bsp_cache_enable(void);

/* HSE 25 MHz -> SYSCLK 480 MHz, PLL1Q 48 MHz for USB OTG FS. */
void bsp_clock_config(void);

/* Run LED on PG7 */
void bsp_led_init(void);
void bsp_led_set(bool on);
void bsp_led_toggle(void);

/* Heartbeat: blinks slowly when idle, quickly while streaming. */
void bsp_led_task(uint32_t period_ms);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_H */
