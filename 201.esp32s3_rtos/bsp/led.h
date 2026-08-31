#ifndef BSP_LED_H
#define BSP_LED_H

#include <Arduino.h>
#include "config/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED driver. The pin is taken from config.h (LED_PIN / LED_ACTIVE_HIGH),
 * so the driver layer depends only on config and never on the application. */
void led_init(void);
void led_set(bool on);
void led_toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
