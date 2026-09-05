/* Board support - STM32H743ZIT6 (25 MHz HSE), no RTOS */
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

/* VBUS sensing on PA9. Most core boards do not route VBUS to the MCU and a
 * device-only design does not need it. */
#define OTG_FS_VBUS_SENSE  0

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
void     bsp_init(void);           /* caches, clocks, LED, USB pins + clocks  */
uint32_t board_millis(void);       /* ms since boot                           */
void     board_delay_ms(uint32_t ms);
void     board_led_write(bool on);
void     board_led_toggle(void);
void     board_usb_init(void);     /* TinyUSB needs this before tusb_init()   */

/* Filled in by bsp_init(), handy for verifying the clock tree */
extern uint32_t g_sysclk_hz;
extern uint32_t g_hclk_hz;
extern uint32_t g_pclk1_hz;        /* UART4 kernel clock                      */

#ifdef __cplusplus
}
#endif

#endif /* BSP_H_ */
