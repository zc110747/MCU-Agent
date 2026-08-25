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
/* Toggle interval: bootloader fast blink 200 ms; the test app overrides this
   to 1000 ms (-DLED_BLINK_MS=1000) for a slow 1 Hz heartbeat. */
#ifndef LED_BLINK_MS
#define LED_BLINK_MS    200U
#endif

void BSP_LED_Init(void);
void BSP_LED_On(void);
void BSP_LED_Off(void);
void BSP_LED_Toggle(void);

#endif /* __BSP_LED_H */
