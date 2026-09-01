#ifndef BSP_LED_H
#define BSP_LED_H

#include <Arduino.h>
#include "config/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED driver.
 *   - WS2812B RGB on GPIO48 (LED_IS_WS2812 == 1, default): driven via
 *     Adafruit_NeoPixel; "on" -> GRB color (LED_ON_*), "off" -> black.
 *   - Plain digital GPIO LED: driven via digitalWrite (LED_ACTIVE_HIGH).
 * The driver layer depends only on config and never on the application. */
void led_init(void);
void led_set(bool on);
void led_toggle(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
