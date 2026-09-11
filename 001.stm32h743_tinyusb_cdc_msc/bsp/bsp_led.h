/* ---------------------------------------------------------------------------
 * On-board user LED driver - STM32H743ZIT6 (Luxiaoban / generic H743 core board)
 *
 * Drives the single user LED (PG7). Low-level, no dependencies beyond the HAL.
 * -------------------------------------------------------------------------*/
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx.h"

/* Board wiring (override here if a different board is used). */
#define BSP_LED_GPIO_PORT     GPIOG
#define BSP_LED_GPIO_PIN      GPIO_PIN_7
#define BSP_LED_GPIO_CLK_EN() __HAL_RCC_GPIOG_CLK_ENABLE()
/* Set to 0 if the LED is wired active-low (cathode to the pin). */
#define BSP_LED_ACTIVE_HIGH   1

void bsp_led_init(void);
void bsp_led_write(bool on);
void bsp_led_toggle(void);
