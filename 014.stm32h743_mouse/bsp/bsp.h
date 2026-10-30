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
 * DMA buffer placement
 *
 * The linker script (GCC) and the scatter file (Keil) reserve .dma_buf and
 * .cam_buf as NOLOAD / UNINIT: nothing is ever loaded into them, and the C
 * startup must not touch them, so no line is dirty when the first transfer
 * starts and a plain invalidate cannot drop data.
 *
 * GCC: an array with a section attribute and no initialiser comes out as a
 *      %nobits section, which is exactly what those regions expect.
 *
 * ARMCLANG: the same code produces SHT_PROGBITS. armlink then stores the whole
 *      zero-filled buffer inside the image - 75 KB of camera frames - and the
 *      scatter loader copies it back at boot, dirtying exactly the lines the
 *      design says must stay clean. Neither __attribute__((zero_init)) (not
 *      supported by armclang, only by armcc) nor zero_init combined with a
 *      section attribute helps; #pragma clang section bss=... is the only way
 *      to obtain a NOBITS section under a chosen name.
 *
 * Both forms must stay paired around the declaration:
 *
 *     BSP_DMA_BUF_BEGIN(".cam_buf")
 *     static uint8_t s_frame[N] BSP_DMA_BUF_ATTR(".cam_buf");
 *     BSP_DMA_BUF_END()
 * -------------------------------------------------------------------------*/
#define BSP_PRAGMA_(x)          _Pragma(#x)

#if defined(__ARMCC_VERSION) && !defined(__CC_ARM)
#  define BSP_DMA_BUF_BEGIN(sec)  BSP_PRAGMA_(clang section bss=sec)
#  define BSP_DMA_BUF_END()       BSP_PRAGMA_(clang section bss="")
#  define BSP_DMA_BUF_ATTR(sec)   __attribute__((aligned(32)))
#else
#  define BSP_DMA_BUF_BEGIN(sec)
#  define BSP_DMA_BUF_END()
#  define BSP_DMA_BUF_ATTR(sec)   __attribute__((section(sec), aligned(32)))
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
