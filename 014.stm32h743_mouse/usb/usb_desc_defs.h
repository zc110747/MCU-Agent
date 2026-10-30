/* ---------------------------------------------------------------------------
 * usb_desc_defs.h - USB identity shared by the firmware and the PC host tool.
 *
 * The host tool matches on VID/PID + product string before it trusts a device,
 * so the numbers here and Host/MouseHost/UsbIds.cs must stay in step.
 * -------------------------------------------------------------------------*/

#ifndef USB_DESC_DEFS_H_
#define USB_DESC_DEFS_H_

#define USB_VID         0xCafeu
#define USB_PID         0x4020u          /* 0x40xx = MCU-Agent HID family */
#define USB_BCD         0x0200u          /* USB 2.0                       */

#define USB_MANUF_STR   "MCU-Agent"
#define USB_PRODUCT_STR "H743 USB Mouse"

#define USB_FW_MAJOR    1u
#define USB_FW_MINOR    0u

#endif /* USB_DESC_DEFS_H_ */
