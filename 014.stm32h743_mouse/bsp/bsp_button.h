/* ---------------------------------------------------------------------------
 * bsp_button.h - three mouse buttons on plain GPIO, polled, debounced.
 *
 * Wiring (confirmed by the user):
 *     PC0  LEFT    input, internal pull-up, closing to GND -> pressed
 *     PC1  RIGHT   input, internal pull-up, closing to GND -> pressed
 *     MIDDLE       pin NOT specified yet -> compiled out
 *
 * A mechanical switch on a pull-up is the textbook case for a 10 ms settle
 * window: contact bounce lasts 1-5 ms, a human cannot press twice inside
 * 10 ms, and the poll runs far faster than that so no edge is missed.
 *
 * Why polling instead of EXTI: the debounce state machine needs a periodic
 * tick anyway. A poll costs three register reads per button and removes the
 * whole class of "the ISR and the main loop disagree about the pin" bugs. The
 * main loop cycle is well under 1 ms (see main.c), so the added latency is
 * the debounce window itself, not the poll rate.
 * -------------------------------------------------------------------------*/

#ifndef BSP_BUTTON_H_
#define BSP_BUTTON_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Wiring - see the file header for the confirmation status of each pin.
 * -------------------------------------------------------------------------*/
#define BTN_LEFT_PORT        GPIOC
#define BTN_LEFT_PIN         GPIO_PIN_0
#define BTN_LEFT_CLK_EN()    __HAL_RCC_GPIOC_CLK_ENABLE()

#define BTN_RIGHT_PORT       GPIOC
#define BTN_RIGHT_PIN        GPIO_PIN_1
#define BTN_RIGHT_CLK_EN()   __HAL_RCC_GPIOC_CLK_ENABLE()

/* The middle button has no assigned pin yet. Set these three macros and flip
 * BSP_BUTTON_MIDDLE_ENABLED to 1 to bring it up - no other file changes. */
#define BSP_BUTTON_MIDDLE_ENABLED   0
#define BTN_MIDDLE_PORT      GPIOC
#define BTN_MIDDLE_PIN       GPIO_PIN_2
#define BTN_MIDDLE_CLK_EN()  __HAL_RCC_GPIOC_CLK_ENABLE()

/* Both buttons close to ground, so the pin reads 0 while pressed. */
#define BSP_BUTTON_ACTIVE_LOW       1

/* Contact settle window. */
#define BSP_BUTTON_DEBOUNCE_MS      10u

typedef enum
{
    BSP_BUTTON_ID_LEFT = 0,
    BSP_BUTTON_ID_RIGHT,
    BSP_BUTTON_ID_MIDDLE,
    BSP_BUTTON_ID_COUNT
} bsp_button_id_t;

void bsp_button_init(void);

/* Sample the pins and advance every debounce state machine.
 * `now_ms` is the monotonic millisecond counter (board_millis()). */
void bsp_button_poll(uint32_t now_ms);

/* Debounced state as a MOUSE_BUTTON_* mask. Bits are independent, so any
 * combination of simultaneous presses is representable. */
uint8_t bsp_button_get_buttons(void);

/* Milliseconds the button has been continuously held, 0 when released.
 * Diagnostics only - a held mouse button has no second HID state. */
uint32_t bsp_button_hold_ms(bsp_button_id_t id, uint32_t now_ms);

/* Raw (undebounced) level of one button, true = pressed. Exposed for tests
 * and for the STATUS command's wiring diagnostics. */
bool bsp_button_raw_pressed(bsp_button_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUTTON_H_ */
