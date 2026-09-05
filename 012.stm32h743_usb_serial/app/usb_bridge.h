/* ---------------------------------------------------------------------------
 * USB CDC <-> UART ring buffer glue.
 *
 * Responsibilities
 *   - move bytes between the TinyUSB CDC interface and the UART rings
 *   - translate SET_LINE_CODING into a live UART4 reconfiguration
 *   - select hardware RTS/CTS flow control from the standard CDC
 *     SET_CONTROL_LINE_STATE RTS signal (see usb_bridge.c for the rationale)
 *
 * The bridge is fully transparent: it never parses the data stream. All
 * configuration arrives through the standard CDC-ACM control channel, which
 * every host serial driver sends on its own - no in-band commands, no
 * out-of-band tooling required.
 * -------------------------------------------------------------------------*/
#ifndef USB_BRIDGE_H_
#define USB_BRIDGE_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint32_t usb_rx_bytes;     /* CDC OUT -> UART path                        */
  uint32_t usb_tx_bytes;     /* UART -> CDC IN path                         */
  uint32_t usb_tx_busy;      /* times the IN FIFO had no room                */
  uint32_t usb_rx_stall;     /* times we stopped reading (UART ring full)    */
  uint32_t line_sets;        /* SET_LINE_CODING accepted                     */
  uint32_t line_sets_bad;    /* SET_LINE_CODING rejected                     */
  uint32_t breaks;           /* SET_CONTROL_LINE_STATE break sent            */
} usb_stats_t;

extern usb_stats_t g_usb_stats;

void usb_bridge_init(void);

/* Called from the main loop. Never blocks, never spins. */
void usb_bridge_service(void);

#endif /* USB_BRIDGE_H_ */
