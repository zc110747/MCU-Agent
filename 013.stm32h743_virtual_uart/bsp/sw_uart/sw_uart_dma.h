/* ---------------------------------------------------------------------------
 * Software UART - TX bit-stream DMA abstraction.
 *
 * Owns the DMA stream so no other module names a stream or a request line.
 * The transfer is memory-to-peripheral into GPIOx->BSRR: one timer update
 * event moves exactly one BSRR word, so one UART bit is one transfer and one
 * byte is SW_UART_BITS_PER_BYTE transfers.
 * -------------------------------------------------------------------------*/
#ifndef SW_UART_DMA_H
#define SW_UART_DMA_H

#include <stdint.h>
#include <stdbool.h>
#include "sw_uart_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Publish the BSRR staging buffer before init: the address is handed to
 * HAL_DMA_Start_IT on every launch. */
int sw_uart_dma_set_buffer(const uint32_t *bsrr_words);

/* Configure the stream for memory-to-BSRR transfers. */
int sw_uart_dma_init(void);

/* Arm the stream for item_count words. */
int sw_uart_dma_start(uint16_t item_count);

/* Halt the stream and drop any pending request. */
void sw_uart_dma_stop(void);

/* True while the stream still owns the buffer. */
bool sw_uart_dma_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* SW_UART_DMA_H */
