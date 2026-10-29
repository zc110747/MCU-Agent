#ifndef BSP_LED_H
#define BSP_LED_H

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED0 -> PB1, LED1 -> PB0 (low active) */
#define LED0_PIN GPIO_PIN_1
#define LED0_PORT GPIOB
#define LED1_PIN GPIO_PIN_0
#define LED1_PORT GPIOB

void bsp_led_init(void);
void bsp_led_on(uint8_t led);
void bsp_led_off(uint8_t led);
void bsp_led_toggle(uint8_t led);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
