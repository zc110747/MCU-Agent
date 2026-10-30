/* ---------------------------------------------------------------------------
 * usb_descriptors.h - standard HID mouse descriptors.
 * -------------------------------------------------------------------------*/

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Interface / endpoint layout, shared with usb_descriptors.c. */
#define USB_ITF_NUM_HID      0u
#define USB_ITF_NUM_TOTAL    1u
#define USB_EPNUM_HID_IN     0x81u
#define USB_HID_EP_SIZE      16u
#define USB_HID_POLL_MS      1u
#define USB_STRID_HID_ITF    4u

/* Accepts the decoded HID report length declared by the descriptor. Returns
 * MOUSE_REPORT_WIRE_LEN; the static assertion in usb_descriptors.c keeps the
 * descriptor and mouse_report.h in sync at compile time. */
uint16_t usb_descriptors_report_len(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_DESCRIPTORS_H_ */
