/* ---------------------------------------------------------------------------
 * mouse_report.h - the one place that knows what the USB wire format is.
 *
 * mouse_report_t is the software-wide event structure. It is NOT what goes on
 * the wire: the HID report descriptor declares 8-bit relative X/Y/Wheel/Pan
 * fields, so a 16-bit accumulator must be split before it can be transmitted
 * (see mouse_core.c). Keeping the two apart is what makes it possible to
 * change the sensor resolution without touching the USB layer, and vice versa.
 * -------------------------------------------------------------------------*/

#ifndef MOUSE_REPORT_H_
#define MOUSE_REPORT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Button bits - identical ordering to the HID Button page (usage 1..3). */
#define MOUSE_BUTTON_LEFT     0x01u
#define MOUSE_BUTTON_RIGHT    0x02u
#define MOUSE_BUTTON_MIDDLE   0x04u
#define MOUSE_BUTTON_MASK     0x07u

/* Report ID declared by usb_descriptors.c. Sending with a report ID costs one
 * extra byte on the wire and makes the report unambiguous for Raw Input
 * parsers on the host. */
#define MOUSE_REPORT_ID       0x01u

/* Wire payload: buttons, x, y, wheel, pan (the report ID is prepended by
 * TinyUSB, so it is not part of this array). */
#define MOUSE_REPORT_WIRE_LEN 5u

/* Largest magnitude a single HID field can carry: logical range is -127..127,
 * not -128..127, because 0x80 is reserved for a different valid range mapping
 * on some hosts and -127..127 is what every reference descriptor uses. */
#define MOUSE_REPORT_AXIS_MAX 127

/* Unified internal event. */
typedef struct
{
    uint8_t buttons;   /* MOUSE_BUTTON_* bit mask                          */
    int16_t x;         /* relative horizontal, + = right                   */
    int16_t y;         /* relative vertical,   + = down                    */
    int8_t  wheel;     /* vertical scroll,     + = away from the user      */
    int8_t  pan;       /* horizontal scroll,   + = right (reserved)        */
} mouse_report_t;

/* Zero a report. */
void mouse_report_clear(mouse_report_t *report);

/* True when the report would not move the cursor nor change any button -
 * such a report must not be sent, it only burns USB bandwidth. */
bool mouse_report_is_idle(const mouse_report_t *report);

/* Serialise into the wire layout. Values outside +-MOUSE_REPORT_AXIS_MAX are
 * clamped; the caller (mouse_core) is responsible for splitting a large
 * displacement so that clamping never silently discards motion. Returns the
 * number of bytes written (MOUSE_REPORT_WIRE_LEN), or 0 on a NULL pointer. */
uint32_t mouse_report_pack(const mouse_report_t *report, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MOUSE_REPORT_H_ */
