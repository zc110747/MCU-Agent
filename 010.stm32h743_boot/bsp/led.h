/**
  ******************************************************************************
  * @file    bsp/led.h
  * @brief   Status LED driver (PG7, active-low, push-pull, pull-up)
  *
  * The LED is driven LOW to turn on, HIGH to turn off (per hardware convention).
  * A 500 ms heartbeat blink signals that the firmware is running.
  ******************************************************************************
  */
#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "stm32h7xx_hal.h"

/* LED on the LXB743ZI-P1 board */
#define LED_GPIO_PORT   GPIOG
#define LED_GPIO_PIN    GPIO_PIN_7
#define LED_BLINK_MS    500U   /* toggle interval: 500 ms -> 1 Hz heartbeat */

void BSP_LED_Init(void);
void BSP_LED_On(void);
void BSP_LED_Off(void);
void BSP_LED_Toggle(void);

#endif /* __BSP_LED_H */
