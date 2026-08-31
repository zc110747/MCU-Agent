#ifndef BSP_BUTTON_H
#define BSP_BUTTON_H

#include <Arduino.h>
#include "config/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Button driver. Debounced edge detection: button_poll_pressed() returns
 * true exactly once per physical press, so the producer task never has to
 * implement debouncing itself. */
void button_init(void);
bool button_poll_pressed(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUTTON_H */
