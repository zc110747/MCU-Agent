/* ---------------------------------------------------------------------------
 * Software UART - bit-timing timer abstraction.
 *
 * Owns PSC/ARR for both bit clocks. No other module touches a TIM register,
 * so the bit-timing arithmetic lives in exactly one place.
 * -------------------------------------------------------------------------*/
#ifndef SW_UART_TIMER_H
#define SW_UART_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "sw_uart_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolved timing for one baud rate. */
typedef struct
{
    uint32_t baudrate;       /* configured baud                            */
    uint32_t timer_clk_hz;    /* kernel clock feeding this timer            */
    uint32_t psc;             /* prescaler, PSC - 1 is written to the reg  */
    uint32_t arr;             /* period, ARR + 1 is written to the reg      */
    uint32_t bit_ticks;       /* timer ticks in one UART bit = arr + 1     */
    uint32_t bit_ns;          /* bit period in ns, for reporting           */
    double   error_pct;       /* vs the ideal bit period, signed            */
} sw_uart_timer_info_t;

/* Configure both bit clocks for the given baud rate. */
int sw_uart_timer_init(uint32_t baudrate);

/* Release the timers. */
void sw_uart_timer_deinit(void);

/* Run/stop the TX bit clock (which drives the DMA requests). */
void sw_uart_timer_start(void);
void sw_uart_timer_stop(void);

/* Kernel clock of the timer that carries the TX bit clock. */
uint32_t sw_uart_timer_get_tick_hz(void);

/* Resolved timing of the currently configured baud rate. */
const sw_uart_timer_info_t *sw_uart_timer_get_info(void);

/* RX sampling clock. The first delay is half a bit time so the start bit is
 * sampled at its centre; every later slot is one bit time, landing on the
 * centre of each following bit. */
void sw_uart_timer_rx_start_half_bit(void);
void sw_uart_timer_rx_reload_one_bit(void);
void sw_uart_timer_rx_stop(void);

/* The RX timer handle, for HAL_TIM_IRQHandler. */
TIM_HandleTypeDef *sw_uart_timer_rx_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* SW_UART_TIMER_H */
