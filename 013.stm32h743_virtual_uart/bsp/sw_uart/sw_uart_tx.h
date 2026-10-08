/* ---------------------------------------------------------------------------
 * Software UART - TX FIFO, bit encoder and DMA chain.
 *
 * Concurrency
 * -----------
 * Two contexts drive this file:
 *   - application:   sw_uart_tx_write() appends, and on the idle -> active
 *                    edge encodes the first chunk itself
 *   - DMA interrupt: on_dma_done() chains the next chunk
 *
 * Only the writer moves head; tail is moved by whoever runs the encoder, so
 * the ring is not strictly single-consumer. What keeps it safe is that the
 * encoder runs only when the state says no chunk is in flight, and the DMA
 * interrupt has the highest priority, so the writer is never mid-encode when
 * it fires. Only this module launches the DMA, from one place (pump), so the
 * stream can never be started twice.
 *
 * tail therefore trails the wire by up to one chunk: pending() and space()
 * describe what is left in the ring, not what is left to transmit.
 * -------------------------------------------------------------------------*/
#ifndef SW_UART_TX_H
#define SW_UART_TX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "sw_uart_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* TX state machine. */
typedef enum
{
    SW_UART_TX_IDLE = 0,      /* FIFO drained, timer parked      */
    SW_UART_TX_ACTIVE,        /* DMA shifting bits out           */
} sw_uart_tx_state_t;

/* Counters for the stress tests. */
typedef struct
{
    uint32_t tx_bytes;           /* bytes accepted into the FIFO   */
    uint32_t tx_dropped;         /* bytes refused, FIFO was full   */
    uint32_t dma_restart_count;  /* DMA launches, incl. chainings  */
    uint32_t tx_frames;          /* bytes actually put on the wire  */
} sw_uart_tx_stats_t;

int  sw_uart_tx_init(void);
void sw_uart_tx_deinit(void);

/* Queue bytes. Never blocks: returns the number accepted, which is less than
 * len when the FIFO filled up. */
size_t sw_uart_tx_write(const uint8_t *data, size_t len);

/* Free space / bytes waiting. */
size_t sw_uart_tx_space(void);
size_t sw_uart_tx_pending(void);

/* Park the bit clock and return the line to the idle level. */
void sw_uart_tx_stop(void);

const sw_uart_tx_stats_t *sw_uart_tx_get_stats(void);
sw_uart_tx_state_t sw_uart_tx_get_state(void);

/* Transfer-complete hook, driven by the DMA interrupt. */
void sw_uart_tx_on_dma_done(void);

#ifdef __cplusplus
}
#endif

#endif /* SW_UART_TX_H */
