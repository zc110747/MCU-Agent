/* ---------------------------------------------------------------------------
 * usb_hid_mouse.h - the only file that talks to TinyUSB for HID output.
 *
 * mouse_core owns the retry/pacing policy, so this layer stays deliberately
 * dumb: pack, check readiness, hand over, report success. Everything it knows
 * about "busy" is what tud_hid_ready() says.
 * -------------------------------------------------------------------------*/

#ifndef USB_HID_MOUSE_H_
#define USB_HID_MOUSE_H_

#include <stdint.h>
#include <stdbool.h>
#include "mouse_report.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t sent;          /* reports accepted by TinyUSB                 */
    uint32_t refused;       /* tud_hid_report() returned false             */
    uint32_t not_ready;     /* skipped because the endpoint was not ready  */
    uint32_t mounted_events;
} usb_hid_mouse_stats_t;

void usb_hid_mouse_init(void);

/* mouse_core_tx_fn compatible. Returns false without consuming anything when
 * the endpoint cannot take the report right now - the caller keeps its state
 * and tries again, which is the whole retry mechanism. */
bool usb_hid_mouse_send(const mouse_report_t *report);

/* True when a host is configured and the IN endpoint has room. */
bool usb_hid_mouse_ready(void);

/* True while the device is enumerated and configured by a host. */
bool usb_hid_mouse_mounted(void);

void usb_hid_mouse_get_stats(usb_hid_mouse_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* USB_HID_MOUSE_H_ */
