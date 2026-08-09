/*----------------------------------------------------------------------------
 * Board support - STM32H743ZIT6 (鹿小班 H743 core board) acting as a
 * CMSIS-DAP v1 debug probe.
 *--------------------------------------------------------------------------*/
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
 *
 * The status LED (PG7) is owned by the CMSIS-DAP layer - DAP_SETUP() puts it
 * in output mode and DAP.c drives it through LED_CONNECTED_OUT() whenever a
 * host connects or disconnects. See bsp/DAP_config.h.
 *
 * VBUS sensing is off: this board does not route VBUS to the MCU, and a
 * bus-powered device-only design does not need it.
 * -------------------------------------------------------------------------*/
#define OTG_FS_VBUS_SENSE  0
#define OTG_HS_VBUS_SENSE  0

/* ---------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------*/
void     bsp_init(void);          /* cache, clocks, USB pins + clocks */
uint32_t board_millis(void);      /* ms since boot                    */
void     board_delay_ms(uint32_t ms);

/** 96-bit unique device ID, formatted as 24 hex chars + NUL. */
void     board_get_unique_id(char *out, uint32_t out_size);

/* Reported by bsp_init(), handy for sanity-checking the clock tree. */
extern uint32_t g_sysclk_hz;
extern uint32_t g_hclk_hz;

#ifdef __cplusplus
}
#endif

#endif /* BSP_H_ */
