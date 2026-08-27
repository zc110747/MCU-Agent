#ifndef USB_HOST_APP_H
#define USB_HOST_APP_H

#include <stdint.h>
#include <stdbool.h>

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

/* FatFs access serialization lock.  ALL FatFs entry points (file_task's
 * explore AND the UI font task's open/glyph-read) must be wrapped with these,
 * otherwise concurrent access from two tasks corrupts the shared MSC
 * _disk_busy flag and deadlocks the disk I/O.  Defined in usb_host_app.c. */
void fs_lock(void);
void fs_unlock(void);

/* Called from main() BEFORE vTaskStartScheduler: creates the file task and
 * prepares the USB Host state.  tusb_init() itself is done in main() so the
 * init ordering (SDRAM -> FreeRTOS heap -> USB) is explicit. */
bool usbh_app_init(void);

/* The USB Host task body: drives the TinyUSB host stack (tuh_task).  Created
 * by main().  Must run so enumeration and the MSC disk-IO callbacks fire. */
void usbh_host_task(void *arg);

#endif /* USB_HOST_APP_H */
