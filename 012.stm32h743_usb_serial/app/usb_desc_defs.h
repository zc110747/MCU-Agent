/* Identity shared by the USB descriptors, the AT command handler and the PC
 * stress tool. The tool matches on VID/PID + product string so it can prove
 * the COM port it opened really is this firmware. */
#ifndef USB_DESC_DEFS_H_
#define USB_DESC_DEFS_H_

#define USB_VID         0xCafeu
#define USB_PID         0x4012u          /* 0x40xx = MCU-Agent bridge family */
#define USB_BCD         0x0200u          /* USB 2.0                          */

#define USB_MANUF_STR   "MCU-Agent"
#define USB_PRODUCT_STR "H743 CDC UART4 Bridge"

#define USB_FW_MAJOR    1u
#define USB_FW_MINOR    0u

#endif /* USB_DESC_DEFS_H_ */
