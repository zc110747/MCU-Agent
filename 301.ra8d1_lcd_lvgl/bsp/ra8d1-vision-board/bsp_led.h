/**
 * @file bsp_led.h
 * @brief On-board LED (P102) driver for RA8D1 Vision Board
 */
#ifndef BSP_LED_H
#define BSP_LED_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void bsp_led_init(void);
void bsp_led_write(bool on);
void bsp_led_toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
