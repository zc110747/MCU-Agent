/* ---------------------------------------------------------------------------
 * bsp_uart.h - UART4 command channel: DMA + IDLE receive, lock-free-ish send.
 *
 * Wiring (TBC - inherited from 012.stm32h743_usb_serial, same core board):
 *     PA0  UART4_TX   AF8
 *     PA1  UART4_RX   AF8
 *     115200 8N1, no flow control
 *
 * Receive path
 *     DMA1_Stream0 in CIRCULAR mode writes into a small staging buffer. The
 *     USART IDLE interrupt (plus the DMA transfer-complete interrupt, which
 *     covers a host that never pauses) moves whatever arrived into a software
 *     ring. Circular DMA is what makes "the host sent 300 bytes with no gap"
 *     work: a NORMAL-mode stream would stop after its first buffer.
 *
 * Transmit path
 *     A TX ring drained straight into TDR while TXE is set. No HAL call, so
 *     nothing here can block the main loop - when the FIFO is full the poll
 *     simply returns and the next pass continues. Responses are ~40 bytes at
 *     115200 (3.5 ms), which is far cheaper than a 1 ms blocking wait.
 *
 * Cache
 *     The DMA staging buffer lives in AXI SRAM (.dma_buf, see the linker
 *     script) and is invalidated before every read, because the DMA wrote to
 *     it behind the D-cache's back.
 * -------------------------------------------------------------------------*/

#ifndef BSP_UART_H_
#define BSP_UART_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_UART_BAUDRATE        115200u

/* DMA staging area. One frame of 256 bytes covers a full burst of commands;
 * anything longer is still handled, it just arrives in several IDLE chunks. */
#define BSP_UART_DMA_RX_SIZE     256u
#define BSP_UART_RX_RING_SIZE    1024u
#define BSP_UART_TX_RING_SIZE    1024u

typedef struct
{
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_overflow;      /* ring full - oldest bytes kept, new dropped  */
    uint32_t tx_overflow;      /* write() found no room for the whole buffer  */
    uint32_t idle_events;
    uint32_t dma_events;
    uint32_t frame_errors;     /* ORE/FE/NE/PE seen and cleared               */
} bsp_uart_stats_t;

void bsp_uart_init(void);

/* Main-loop pump: moves TX ring -> TDR as long as the FIFO accepts it.
 * Never blocks, safe to call on every pass. */
void bsp_uart_poll(void);

/* Copy up to `max_len` received bytes out of the ring. Returns the count. */
uint32_t bsp_uart_read(uint8_t *dst, uint32_t max_len);

/* Queue bytes for transmission. Returns the number actually queued, which is
 * less than `len` when the ring filled up (the caller's loss is counted). */
uint32_t bsp_uart_write(const uint8_t *src, uint32_t len);

/* Convenience wrapper for NUL-terminated ASCII, e.g. protocol replies. */
uint32_t bsp_uart_write_str(const char *text);

/* Bytes sitting in the RX ring. The protocol layer uses this to avoid
 * copying when there is nothing to parse. */
uint32_t bsp_uart_available(void);

void bsp_uart_get_stats(bsp_uart_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H_ */
