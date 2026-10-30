/* ---------------------------------------------------------------------------
 * bsp.h - board resources for the STM32H743ZI USB HID mouse.
 *
 * Every pin below is either (a) confirmed by the user, or (b) inherited from a
 * board-verified sibling project in this repository. Anything that is NOT
 * confirmed carries an explicit "TBC" marker and is kept behind a feature
 * macro, so a wrong guess can never produce a silently mis-wired build.
 *
 *   CONFIRMED (user, prompt v1):
 *     PC0  button LEFT   - input, internal pull-up, active low
 *     PC1  button RIGHT  - input, internal pull-up, active low
 *
 *   TBC - not yet specified by the user, therefore disabled at build time:
 *     MIDDLE button pin  (BSP_BUTTON_MIDDLE_ENABLED 0)
 *
 *   INHERITED from 012.stm32h743_usb_serial (same core board, verified there):
 *     PA11/PA12  USB2_OTG_FS DM/DP       UART4 PA0(TX)/PA1(RX) AF8
 *     PG7        LED
 *
 *   INHERITED from 002.stm32h743_tinyusb_uvc_ov5640 (verified there):
 *     DCMI 8-bit DVP + OV5640 on I2C4 (PF14/PF15), PWDN PF13
 * -------------------------------------------------------------------------*/

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

/* Device-only design: VBUS is not routed to the MCU on this core board. */
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
