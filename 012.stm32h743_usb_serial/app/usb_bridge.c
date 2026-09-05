/* ---------------------------------------------------------------------------
 * USB CDC <-> UART4 transparent bridge (STM32H743, no RTOS).
 *
 * Design
 * ------
 * The device is a *transparent* serial bridge: every byte the host writes to
 * the CDC bulk-OUT endpoint is forwarded to UART4, and every byte UART4
 * receives is forwarded to the CDC bulk-IN endpoint. Nothing in the data
 * stream is ever interpreted, buffered-as-a-command, or held back - so a host
 * application that tunnels its own "AT" or binary protocol through the port is
 * never intercepted by the firmware.
 *
 * Configuration uses ONLY the standard CDC-ACM control channel (EP0), which
 * every host serial driver sends automatically - there is no in-band escape
 * sequence and no out-of-band tooling required:
 *
 *   SET_LINE_CODING       -> baud / data bits / parity / stop bits.
 *                            Reconfigures UART4 live, via tud_cdc_line_coding_cb.
 *   SET_CONTROL_LINE_STATE-> DTR and RTS, the standard modem-control signals.
 *                            These are passed straight through to the UART:
 *                              RTS -> our RTS pin (PB14), i.e. the signal we
 *                                     send the UART peer to say "I can accept
 *                                     data". It is ANDed with the RX-ring
 *                                     headroom so we also protect the 16 KB ring.
 *                              DTR -> observed only; UART4 has no DTR/DSR pins.
 *                            Hardware CTS flow control (TX gated by the peer's
 *                            CTS on PB0) is ALWAYS on at the USART level and
 *                            needs no host switch - a peer that does not drive
 *                            CTS simply leaves it floating (PULLDOWN = ready),
 *                            so its TX is never gated.
 *
 * This is fully transparent for all three host cases: single RTS, single DTR,
 * and dual RTS/DTR all just work, because they are standard signal pass-through
 * rather than firmware-mode switches. The bridge never gates the data path on
 * DTR: a transparent bridge delivers bytes regardless of it.
 * -------------------------------------------------------------------------*/

#include "usb_bridge.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "tusb.h"
#include "uart_bridge.h"
#include "ringbuf.h"
#include "bsp.h"

#define CDC_READ_CHUNK  256u   /* bytes per loop from the CDC RX FIFO */

usb_stats_t g_usb_stats;

/* ---------------------------------------------------------------------------
 * Data path: USB -> UART (always transparent, never inspected)
 * -------------------------------------------------------------------------*/
static void data_out(const uint8_t *src, uint32_t len) {
  if (len == 0u) return;
  uint32_t accepted = uart_bridge_write(src, len);
  g_usb_stats.usb_rx_bytes += accepted;
  if (accepted < len) {
    /* Should not happen: we only read as much as the ring can take. */
    g_usb_stats.usb_rx_stall += (len - accepted);
  }
}

/* ---------------------------------------------------------------------------
 * USB scheduling
 * -------------------------------------------------------------------------*/
static void cdc_rx_service(void) {
  uint8_t  buf[CDC_READ_CHUNK];
  uint32_t space;
  uint32_t want;
  uint32_t got;

  if (!tud_cdc_available()) return;

  /* Only take what we can actually store in the UART TX ring. Leaving the rest
   * in TinyUSB's RX FIFO stops it re-arming the OUT endpoint, so the host is
   * NAK'd - real USB-level back-pressure instead of silent loss. */
  space = uart_bridge_tx_free();
  if (space == 0u) {
    g_usb_stats.usb_rx_stall++;
    return;
  }

  want = tud_cdc_available();
  if (want == 0u) return;
  if (want > space)        want = space;
  if (want > sizeof(buf))  want = sizeof(buf);

  /* Read exactly what we have room for - anything else stays in TinyUSB's
   * FIFO and keeps the OUT endpoint from being re-armed. */
  got = tud_cdc_read(buf, want);
  data_out(buf, got);
}

static void cdc_tx_service(void) {
  uint32_t avail;
  uint32_t len;
  const uint8_t *p;

  tud_cdc_write_flush();            /* finish whatever is already pending    */

  p = rb_read_ptr(&g_uart_rx_rb, &len);
  if (len == 0u) return;

  avail = tud_cdc_write_available();
  if (len > avail) len = avail;
  if (len == 0u) {
    g_usb_stats.usb_tx_busy++;
    return;
  }

  uint32_t w = tud_cdc_write(p, len);
  if (w) {
    rb_commit_read(&g_uart_rx_rb, w);
    g_usb_stats.usb_tx_bytes += w;
  }
  /* Flush now: a short packet must leave immediately instead of waiting for
   * a full 64-byte USB packet. */
  tud_cdc_write_flush();
}

void usb_bridge_service(void) {
  cdc_rx_service();
  cdc_tx_service();
}

void usb_bridge_init(void) {
  memset(&g_usb_stats, 0, sizeof(g_usb_stats));
}

/* ---------------------------------------------------------------------------
 * TinyUSB callbacks - the ONLY configuration channel (standard CDC)
 * -------------------------------------------------------------------------*/

/* Host changed baud rate / format: reconfigure UART4 on the fly. */
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding) {
  (void) itf;

  uart_cfg_t cfg;
  uart_bridge_get_cfg(&cfg);

  cfg.baud      = p_line_coding->bit_rate;
  cfg.data_bits = p_line_coding->data_bits;
  cfg.parity    = p_line_coding->parity;
  cfg.stop_bits = p_line_coding->stop_bits;

  if (uart_bridge_apply(&cfg) == 0) {
    g_usb_stats.line_sets++;
  } else {
    g_usb_stats.line_sets_bad++;
  }
}

/* Host set DTR/RTS via SET_CONTROL_LINE_STATE. These are the standard CDC
 * modem-control signals and they are passed straight through to the UART:
 *   - RTS -> our RTS pin (PB14), the signal we send the UART peer to say "I
 *           can accept data". Combined with the RX-ring headroom in
 *           uart_flow_service it also protects our 16 KB ring.
 *   - DTR -> observed only; UART4 has no DTR/DSR pins.
 * Hardware CTS flow control (TX gated by the peer's CTS on PB0) is ALWAYS on
 * at the USART level and needs no host switch - a peer that doesn't drive CTS
 * simply leaves it floating (PULLDOWN = ready) so its TX is never gated. This
 * is fully transparent for all three host cases:
 *   single RTS  -> RTS pin follows host RTS
 *   single DTR  -> DTR observed, RTS pin keeps its state
 *   dual RTS/DTR-> both applied
 * DTR never gates the data path: a transparent bridge delivers bytes regardless. */
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
  (void) itf;
  (void) dtr;
  uart_bridge_line_state(rts, dtr);
}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms) {
  (void) itf;
  (void) duration_ms;
  g_usb_stats.breaks++;
}
