/* Board support for the STM32H743ZIT6 board (Luxiaoban / generic H743 core board) */
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
/* Set to 0 if the LED is wired active-low (cathode to the pin). */
#define LED_ACTIVE_HIGH    1

/* VBUS sensing on PA9. Most core boards do not route VBUS to the MCU, and a
 * device-only design does not need it, so it is off: the device then always
 * assumes bus power is present. Set to 1 only if PA9 really is wired to VBUS. */
#define OTG_FS_VBUS_SENSE  0
#define OTG_HS_VBUS_SENSE  0

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
void     bsp_init(void);          /* cache, clocks, LED, USB pins + clocks   */
uint32_t board_millis(void);      /* ms since boot                            */
void     board_delay_ms(uint32_t ms);
void     board_led_write(bool on);
void     board_led_toggle(void);

/* Reported by bsp_init(), handy to print for sanity checking the clock tree */
extern uint32_t g_sysclk_hz;
extern uint32_t g_hclk_hz;

#ifdef __cplusplus
}
#endif

#endif /* BSP_H_ */
