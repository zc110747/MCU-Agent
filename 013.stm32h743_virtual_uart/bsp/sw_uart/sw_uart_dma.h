/* ---------------------------------------------------------------------------
 * Software UART - TX bit-stream DMA abstraction.
 *
 * Owns the DMA stream registers so no other module has to know which stream
 * or request line is in use. The transfer is memory-to-peripheral into
 * GPIOx->BSRR: one timer update event moves exactly one BSRR word, so one UART
 * bit is one DMA transfer and one byte is SW_UART_BITS_PER_BYTE transfers.
 * -------------------------------------------------------------------------*/
#ifndef SW_UART_DMA_H
#define SW_UART_DMA_H

#include <stdint.h>
#include <stdbool.h>
#include "sw_uart_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configure the stream for a memory-to-BSRR transfer of item_count words.
 * The buffer must stay alive and stay cache-clean until the transfer ends. */
int sw_uart_dma_init(const uint32_t *bsrr_words, uint16_t item_count);

/* Arm the stream for item_count words without touching the configuration. */
int sw_uart_dma_start(uint16_t item_count);

/* Halt the stream and drain any pending request. */
void sw_uart_dma_stop(void);

/* True while the stream still owns the buffer. */
bool sw_uart_dma_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* SW_UART_DMA_H */
