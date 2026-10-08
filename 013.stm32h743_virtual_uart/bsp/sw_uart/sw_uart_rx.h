/* ---------------------------------------------------------------------------
 * Software UART - RX start-bit detection, sampling and frame parsing.
 * -------------------------------------------------------------------------*/
#ifndef SW_UART_RX_H
#define SW_UART_RX_H

#include <stdint.h>
#include <stddef.h>
#include "sw_uart_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RX state machine. */
typedef enum
{
    SW_UART_RX_IDLE = 0,      /* waiting for a falling edge on the line */
    SW_UART_RX_START,         /* start bit seen, timing the first sample */
    SW_UART_RX_DATA,          /* shifting in the 8 data bits             */
    SW_UART_RX_STOP,          /* sampling the stop bit                   */
} sw_uart_rx_state_t;

/* Counters for the stress tests. */
typedef struct
{
    uint32_t rx_bytes;        /* frames handed to the FIFO               */
    uint32_t rx_overflow;     /* frames dropped, FIFO was full           */
    uint32_t rx_frame_error;  /* stop bit was not 1, or start was not 0  */
} sw_uart_rx_stats_t;

int  sw_uart_rx_init(void);
void sw_uart_rx_deinit(void);

/* Copy out received bytes; returns the number copied. */
size_t sw_uart_rx_read(uint8_t *data, size_t len);

/* Bytes waiting in the FIFO. */
size_t sw_uart_rx_available(void);

/* Free space left in the RX FIFO. */
size_t sw_uart_rx_free(void);

const sw_uart_rx_stats_t *sw_uart_rx_get_stats(void);
sw_uart_rx_state_t sw_uart_rx_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* SW_UART_RX_H */
