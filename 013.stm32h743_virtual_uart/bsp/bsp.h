/* Board support - STM32H743ZIT6, external 25 MHz crystal, no RTOS */
#ifndef BSP_H_
#define BSP_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Board wiring
 * -------------------------------------------------------------------------*/
#define LED_GPIO_PORT      GPIOG
#define LED_GPIO_PIN       GPIO_PIN_7
#define LED_GPIO_CLK_EN()  __HAL_RCC_GPIOG_CLK_ENABLE()
/* 1 = anode on the pin (write 1 lights it) */
#define LED_ACTIVE_HIGH    1

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
void     bsp_init(void);           /* caches, clocks, LED                    */
uint32_t board_millis(void);       /* ms since boot                           */
void     board_delay_ms(uint32_t ms);
void     board_led_write(bool on);
void     board_led_toggle(void);

/* Filled in by bsp_init(), handy for verifying the clock tree */
extern uint32_t g_sysclk_hz;
extern uint32_t g_hclk_hz;
extern uint32_t g_pclk1_hz;

#ifdef __cplusplus
}
#endif

#endif /* BSP_H_ */
