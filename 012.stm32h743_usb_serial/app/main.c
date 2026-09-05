/* ---------------------------------------------------------------------------
 * STM32H743 - USB CDC <-> UART4 bridge, bare metal (no RTOS)
 *
 *   USB CDC RX -> ring -> UART4 TX DMA -> PA0
 *                                         |  external short
 *   USB CDC TX <- ring <- UART4 RX DMA <- PA1
 *
 * Everything is non-blocking: the loop below just pumps four independent
 * queues. Nothing in it waits for a byte, a DMA transfer or a USB packet.
 *
 * Default: 115200 8N1, no flow control. The host changes the format with the
 * standard SET_LINE_CODING control request, and selects RTS/CTS hardware flow
 * control with the standard SET_CONTROL_LINE_STATE RTS signal (see
 * usb_bridge.c for the polarity). There is no in-band command channel: every
 * byte on the wire is forwarded transparently.
 * -------------------------------------------------------------------------*/

#include <stdint.h>
#include <stdbool.h>

#include "bsp.h"
#include "tusb.h"
#include "uart_bridge.h"
#include "usb_bridge.h"

static volatile bool s_mounted;

/* ---------------------------------------------------------------------------
 * LED: fast blink while un-enumerated, on during traffic, slow heartbeat when
 * idle so you can see the board is alive.
 * -------------------------------------------------------------------------*/
static void led_service(void) {
  static uint32_t last_toggle;
  static uint32_t last_activity;
  static uint32_t last_total;

  uint32_t now   = board_millis();
  uint32_t total = g_uart_stats.rx_bytes + g_uart_stats.tx_bytes +
                   g_usb_stats.usb_tx_bytes;

  if (!s_mounted) {
    if ((uint32_t) (now - last_toggle) >= 125u) {
      last_toggle = now;
      board_led_toggle();
    }
    return;
  }

  if (total != last_total) {
    last_total    = total;
    last_activity = now;
    board_led_write(true);
    return;
  }

  if ((uint32_t) (now - last_activity) > 200u) {
    if ((uint32_t) (now - last_toggle) >= 1000u) {
      last_toggle = now;
      board_led_toggle();
    }
  }
}

/* ------------------------------------------------------------------------ */
int main(void) {
  bsp_init();                       /* caches, 400 MHz clocks, LED          */
  board_usb_init();                 /* PA11/PA12, OTG_FS clock, USB voltage  */

  uart_bridge_init();               /* UART4 + circular RX DMA + TX DMA      */
  usb_bridge_init();                /* CDC <-> UART glue, no command engine   */

  /* VBUS is not routed to the MCU on this board, so tell the DWC2 driver to
   * assume bus power is present. Must be done before tusb_init(). */
  tud_configure_dwc2_t dwc2_cfg = CFG_TUD_CONFIGURE_DWC2_DEFAULT;
  dwc2_cfg.vbus_sensing = false;
  tud_configure(BOARD_TUD_RHPORT, TUD_CFGID_DWC2, &dwc2_cfg);

  const tusb_rhport_init_t rh_init = {
      .role  = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL,
  };
  tusb_init(BOARD_TUD_RHPORT, &rh_init);

  while (1) {
    tud_task();                     /* USB device stack                     */

    /* Order matters only for latency: USB events are processed first, then
     * the four queues are drained once each. */
    usb_bridge_service();           /* CDC OUT -> UART ring, UART ring -> IN */
    uart_rx_service();              /* circular DMA RX buffer -> ring        */
    uart_tx_service();              /* ring -> DMA TX (or wait for CTS)      */
    uart_flow_service();            /* keep the RTS output honest            */

    led_service();
  }
}

/* ------------------------------------------------------------------------ */
void tud_mount_cb(void)   { s_mounted = true;  board_led_write(true); }
void tud_umount_cb(void)  { s_mounted = false; board_led_write(false); }
void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  board_led_write(false);
}
void tud_resume_cb(void)  { board_led_write(s_mounted); }
