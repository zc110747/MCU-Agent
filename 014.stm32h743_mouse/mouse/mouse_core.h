/* ---------------------------------------------------------------------------
 * mouse_core.h - the single place where all mouse inputs meet.
 *
 *            optical motion        GPIO buttons        UART commands
 *                  |                    |                   |
 *                  v                    v                   v
 *              add_motion()      mouse_input_set_*()   add_wheel()/add_pan()
 *                  \____________________|___________________/
 *                                       v
 *                                   mouse_core
 *                                       v
 *                              tx sink (usb_hid_mouse_send)
 *
 * Three rules drive the whole design:
 *
 * 1. MOTION MAY BE MERGED, NEVER DROPPED.
 *    Displacements accumulate in 32-bit accumulators. A single HID field only
 *    carries -127..127, so a large move is split across several reports and
 *    the remainder stays queued. The accumulators saturate instead of wrapping,
 *    and the saturation is counted (never silent).
 *
 * 2. A BUTTON EDGE IS AN EVENT, NOT A LEVEL.
 *    Sending "the current button state" would lose a press that is released
 *    again while the endpoint is busy. Every change is therefore pushed into a
 *    small FIFO of button snapshots and each snapshot is transmitted as its own
 *    report. The FIFO only overwrites when it overflows, and then it keeps the
 *    newest value, so the final state is always consistent.
 *
 * 3. NOTHING IS CONSUMED UNLESS THE TRANSFER SUCCEEDED.
 *    The accumulator is decremented and the FIFO entry popped only after the
 *    sink reported success. A busy or unconfigured endpoint therefore leaves
 *    the pending state untouched - that is the entire retry mechanism.
 * -------------------------------------------------------------------------*/

#ifndef MOUSE_CORE_H_
#define MOUSE_CORE_H_

#include <stdint.h>
#include <stdbool.h>
#include "mouse_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Button snapshots that may wait for the endpoint. Eight is far more than a
 * full-speed HID endpoint can ever need (one report per 1 ms frame), and it
 * costs 8 bytes of RAM. */
#define MOUSE_BTN_QUEUE_DEPTH     8u

/* Accumulator ceiling. USB may stay unplugged for a while; without a ceiling a
 * 32-bit accumulator could take hundreds of reports to drain after a reconnect,
 * which looks like the cursor flying across the screen. Saturation is counted. */
#define MOUSE_PENDING_LIMIT       4096

/* Minimum spacing between two reports, in milliseconds. Matches the 1 ms
 * interrupt endpoint interval declared in the HID descriptor. */
#define MOUSE_TX_INTERVAL_MS      1u

/* Returns true when the report left the device. Must not block. */
typedef bool (*mouse_core_tx_fn)(const mouse_report_t *report);

typedef struct
{
    uint32_t reports_sent;
    uint32_t reports_failed;
    uint32_t button_events;
    uint32_t button_overflow;
    uint32_t pending_saturations;
    int32_t  pending_x;
    int32_t  pending_y;
    int32_t  pending_wheel;
    int32_t  pending_pan;
    uint8_t  buttons_combined;   /* physical | uart, what the host will see */
    uint8_t  buttons_sent;       /* last value that actually went out        */
    uint8_t  button_queue_len;
} mouse_core_status_t;

void mouse_core_init(void);

/* Install the transmit path. Without a sink mouse_core keeps accumulating and
 * simply never drains - useful for bring-up without USB. */
void mouse_core_set_tx(mouse_core_tx_fn tx);

/* Optical motion input. Values are in "counts", already filtered by
 * motion_filter; the core does not interpret magnitude. */
void mouse_core_add_motion(int32_t delta_x, int32_t delta_y);

/* Wheel / pan input. Each notch is +-1 (or +-120 if a host-style encoder is
 * used later - the core only accumulates). */
void mouse_core_add_wheel(int32_t delta_wheel);
void mouse_core_add_pan(int32_t delta_pan);

/* Report a button state change the instant it happens. Both input drivers call
 * this from mouse_input_set_*() so a press that is released again while the
 * endpoint is busy is still delivered as two separate reports. */
void mouse_core_notify_buttons(void);

/* Main-loop pump: emit at most one report per call, only if the sink accepts
 * it. Must be called often - it is the only thing that drains the accumulators. */
void mouse_core_submit(void);

/* Force-drain every pending motion and button edge in one go, ignoring the
 * pacing interval. Used by "RELEASE,ALL" and before a controlled shutdown. */
void mouse_core_flush(void);

/* Called when the bus state changes. On disconnect the pending motion is
 * cleared (it is stale by the time the host returns) and every button is
 * pushed as released, so no button can stay stuck on the host. */
void mouse_core_on_usb_disconnect(void);

void mouse_core_get_status(mouse_core_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* MOUSE_CORE_H_ */
