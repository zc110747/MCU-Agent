/* ---------------------------------------------------------------------------
 * Software UART - public API.
 *
 * A bit-banged UART built only from GPIO, a timer, DMA and EXTI. No hardware
 * USART peripheral is configured: the "baud rate" is only a protocol number
 * that the timer arithmetic turns into a bit period.
 *
 * Frame format is fixed at 8 data bits, 1 stop bit, no parity.
 * -------------------------------------------------------------------------*/
#ifndef SW_UART_H
#define SW_UART_H

#include <stdint.h>
#include <stddef.h>
#include "sw_uart_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Protocol-level configuration. Only the baud rate is variable so far; the
 * frame is fixed at 8N1. */
typedef enum
{
    SW_UART_PARITY_NONE = 0,
    SW_UART_PARITY_EVEN,
    SW_UART_PARITY_ODD,
} sw_uart_parity_t;

typedef struct
{
    uint32_t        baudrate;
    uint8_t         data_bits;
    uint8_t         stop_bits;
    sw_uart_parity_t parity;
} sw_uart_config_t;

/* Default 9600 8N1. */
extern const sw_uart_config_t SW_UART_DEFAULT_CONFIG;

/* Running counters, useful for the stress tests. */
typedef struct
{
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_overflow;
    uint32_t rx_overflow;
    uint32_t rx_frame_error;
    uint32_t dma_restart_count;
} sw_uart_stats_t;

/* Bring up GPIO, timer and DMA. Returns 0 on success. */
int sw_uart_init(const sw_uart_config_t *config);

/* Park the driver and release the bit clock. */
void sw_uart_deinit(void);

/* Queue bytes for transmission. Never blocks: whatever does not fit in the
 * FIFO is dropped and the accepted length is returned. */
size_t sw_uart_write(const uint8_t *data, size_t len);

/* Copy out received bytes; returns the number actually copied. */
size_t sw_uart_read(uint8_t *data, size_t len);

/* Bytes waiting in the RX FIFO. */
size_t sw_uart_available(void);

/* Free space left in the TX FIFO. */
size_t sw_uart_tx_free(void);

/* Snapshot of the running counters. */
const sw_uart_stats_t *sw_uart_get_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* SW_UART_H */
