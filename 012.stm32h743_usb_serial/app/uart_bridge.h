/* ---------------------------------------------------------------------------
 * UART4 <-> ring buffer bridge (STM32H743, no RTOS, no blocking UART calls)
 *
 *   USB -> UART : ring buffer g_uart_tx_rb  -> DMA1_Stream1 (normal mode)
 *   UART -> USB : DMA1_Stream0 (circular)   -> ring buffer g_uart_rx_rb
 *
 * RX design
 * ---------
 * The DMA runs forever in circular mode over a 2 KB buffer; it is never
 * stopped and never reloaded, so it cannot "fill up and stall".
 *
 * The DMA write position is derived from the NDTR register, not from the
 * interrupt that happened:
 *
 *      wr = (RX_DMA_SIZE - NDTR) & (RX_DMA_SIZE - 1)
 *
 * Because every consumer simply drains [head, wr), the three events
 * (Half Transfer, Transfer Complete, IDLE) become *hints* rather than
 * obligations:
 *   - IDLE fires at the end of every frame  -> short packets get out quickly
 *     even when the buffer is nearly empty;
 *   - HT / TC fire in the middle of a continuous stream -> bulk data is moved
 *     in large chunks;
 *   - several events firing together, or an event being missed entirely,
 *     changes nothing: the drain is idempotent, so no byte is ever read twice
 *     and none is skipped.
 *
 * Cache handling (Cortex-M7 D-cache is on, no MPU "non-cacheable" region)
 * ----------------------------------------------------------------------
 *   RX: SCB_InvalidateDCache_by_Addr() over the 32-byte-aligned span that
 *       covers the bytes about to be copied out.
 *   TX: SCB_CleanDCache_by_Addr() over the staging buffer after the copy in.
 *   Both buffers live in the NOLOAD ".dma_buf" section (AXI SRAM 0x24000000,
 *   reachable by DMA1/DMA2 unlike DTCM), start on a 32-byte boundary and have
 *   32-byte-multiple sizes, so a cache line is never shared with foreign data
 *   and a Clean/Invalidate can never destroy somebody else's data.
 *
 * Flow control (hardware, connection-gated, transparent)
 * ----------------------------------------------------------------------
 *   The CTS/RTS hardware-flow-control bits are ENABLED ONLY while the USB CDC
 *   port is open, and DISABLED on disconnect - at which point the UART reverts
 *   to its default 115200 / 8N1 / no-parity with flow control off.
 *     - "Port open" is detected from the host DTR signal (SET_CONTROL_LINE_STATE):
 *       DTR asserted => connected => flow control armed; DTR deasserted =>
 *       disconnected => revert to default, flow control off. (DTR is the only
 *       modem signal Windows usbser.sys forwards reliably; RTS is not re-sent
 *       after open, so it cannot be used as the enable trigger.)
 *   CTS (PB0): UART4 hardware flow-control input, sampled by the USART in
 *              hardware (CTSE). When armed, TX halts the instant the *peer*
 *              deasserts CTS and resumes on its own. A peer that does not
 *              implement flow control leaves CTS floating; PULLDOWN reads that
 *              as "ready", so its TX is never gated. When flow control is
 *              disarmed (disconnect) CTSE is cleared and the peer can never
 *              gate our TX regardless of CTS.
 *   RTS (PB14): software GPIO output, armed only while connected. The host RTS
 *              signal is passed through to it, ANDed with the RX-ring headroom
 *              in uart_flow_service so the 16 KB ring is protected. This is the
 *              RTS we send to the UART peer. When disconnected the pin is held
 *              inactive (high) and uart_flow_service leaves it alone.
 *   DTR: UART4 has no DTR/DSR pins, so the host DTR signal is observed only
 *        (see uart_bridge_dtr_asserted); it is, however, the connection signal
 *        that arms/disarms flow control.
 * -------------------------------------------------------------------------*/
#ifndef UART_BRIDGE_H_
#define UART_BRIDGE_H_

#include <stdint.h>
#include <stdbool.h>
#include "ringbuf.h"

/* Sizes. RX/TX DMA buffers must be powers of two (index wrap) and multiples
 * of 32 bytes (cache lines). */
#define UART_RX_DMA_SIZE   2048u
#define UART_TX_DMA_SIZE   1024u
#define UART_RX_RB_SIZE    16384u
#define UART_TX_RB_SIZE    4096u

typedef struct {
  uint32_t baud;
  uint8_t  data_bits;   /* 7 or 8 logical data bits (5/6 not supported)   */
  uint8_t  parity;      /* 0 none, 1 odd, 2 even                          */
  uint8_t  stop_bits;   /* 0 = 1, 1 = 1.5, 2 = 2                          */
} uart_cfg_t;

typedef struct {
  uint32_t rx_bytes;        /* DMA -> ring                                */
  uint32_t tx_bytes;        /* ring -> DMA                                */
  uint32_t rx_idle;         /* IDLE interrupts                            */
  uint32_t rx_ht;           /* DMA half-transfer interrupts               */
  uint32_t rx_tc;           /* DMA transfer-complete interrupts           */
  uint32_t rx_drain_calls;  /* number of drains that moved data           */
  uint32_t rx_max_span;     /* largest single drain, bytes                */
  uint32_t rx_ring_dropped; /* bytes lost: ring full (DMA never stopped)  */
  uint32_t tx_ring_dropped; /* bytes rejected: TX ring full              */
  uint32_t tx_chunks;       /* DMA TX transfers started                   */
  uint32_t cts_waits;       /* TX chunks deferred because CTS was off     */
  uint32_t rts_off;         /* times RTS was deasserted (RX ring full)    */
  uint32_t overrun_err;     /* USART overrun (ORE)                        */
  uint32_t frame_err;
  uint32_t parity_err;
  uint32_t noise_err;
  uint32_t reconf_ok;
  uint32_t reconf_fail;
} uart_stats_t;

extern ringbuf_t    g_uart_rx_rb;   /* UART -> USB  */
extern ringbuf_t    g_uart_tx_rb;   /* USB  -> UART */
extern uart_stats_t g_uart_stats;
extern uart_cfg_t   g_uart_cfg;

void uart_bridge_init(void);

/* Apply a new serial configuration (from SET_LINE_CODING). Non-blocking.
 * Returns 0 on success, negative on an unsupported combination; the previous
 * configuration keeps running if it fails. */
int  uart_bridge_apply(const uart_cfg_t *cfg);
void uart_bridge_get_cfg(uart_cfg_t *cfg);
void uart_bridge_line_state(bool host_rts, bool host_dtr);

/* Called from the main loop. Bounded and never block.
 *   uart_rx_service      - drain the circular DMA buffer into the ring
 *   uart_tx_service      - start the next DMA chunk if the link is idle
 *   uart_flow_service    - update the RTS output from the RX headroom (only
 *                          while flow control is armed)
 *   uart_bridge_connection_service - arm/disarm CTS/RTS flow control when the
 *                          host DTR (port open/close) changes; on close revert
 *                          the UART to 115200/8N1/none with flow control off. */
void uart_rx_service(void);
void uart_tx_service(void);
void uart_flow_service(void);
void uart_bridge_connection_service(void);

/* Ring accessors used by the USB side. */
uint32_t uart_bridge_write(const uint8_t *src, uint32_t len);  /* -> UART */
uint32_t uart_bridge_read(uint8_t *dst, uint32_t len);         /* <- UART */
uint32_t uart_bridge_tx_free(void);
uint32_t uart_bridge_rx_used(void);
bool     uart_bridge_tx_idle(void);

/* Line state sampled at ISR time, for statistics / diagnostics. */
bool     uart_bridge_cts_asserted(void);
bool     uart_bridge_rts_asserted(void);   /* state of our own RTS output    */
bool     uart_bridge_dtr_asserted(void);   /* host DTR observed (no UART pin)*/

uint32_t uart_bridge_clock_hz(void);

#endif /* UART_BRIDGE_H_ */
