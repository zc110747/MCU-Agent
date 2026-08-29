#ifndef USB_HOST_APP_H
#define USB_HOST_APP_H

#include <stdint.h>
#include <stdbool.h>
#include "fs_diskio.h"

/* USB disk status state machine (LED1 reflects this). */
typedef enum
{
  USB_DISCONNECTED = 0,
  USB_CONNECTED,
  USB_ENUMERATED,
  USB_MSC_READY,
  USB_MOUNTED,
  USB_ERROR
} usb_state_t;

extern volatile usb_state_t g_usb_state;

/* FatFs access serialization lock (fs_lock/fs_unlock) is declared in
 * fs_diskio.h and defined in fs_diskio.c, which owns the combined
 * USB-MSC + microSD diskio glue. */

/* Called from main() BEFORE vTaskStartScheduler: creates the file task and
 * prepares the USB Host state.  tusb_init() itself is done in main() so the
 * init ordering (SDRAM -> FreeRTOS heap -> USB) is explicit. */
bool usbh_app_init(void);

/* The USB Host task body: drives the TinyUSB host stack (tuh_task).  Created
 * by main().  Must run so enumeration and the MSC disk-IO callbacks fire. */
void usbh_host_task(void *arg);

#endif /* USB_HOST_APP_H */
