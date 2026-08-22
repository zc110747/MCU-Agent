#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "stm32f4xx_hal.h"

/* LED0 -> PB1, LED1 -> PB0 (low active) */
#define LED0_PIN  GPIO_PIN_1
#define LED0_PORT GPIOB
#define LED1_PIN  GPIO_PIN_0
#define LED1_PORT GPIOB

void BSP_LED_Init(void);
void BSP_LED_On(uint8_t led);
void BSP_LED_Off(uint8_t led);
void BSP_LED_Toggle(uint8_t led);

#endif /* __BSP_LED_H */
