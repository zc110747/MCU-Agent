/**
  ******************************************************************************
  * @file    drv_usb_cdc.h
  * @brief   USB CDC-ACM virtual COM port (tinyusb on USB2_OTG_FS, PA11/PA12).
  *
  *  Everything here is non-blocking.  If no host has the port open the writes
  *  are discarded, so a firmware that logs heavily never stalls just because
  *  the USB cable is unplugged.
  ******************************************************************************
  */
#ifndef __DRV_USB_CDC_H
#define __DRV_USB_CDC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
  * @brief  Bring up the 48 MHz USB clock, the PHY pins and the device stack.
  * @retval 0 on success, -1 when the USB clock could not be configured.
  */
int      drv_usb_cdc_init(void);

/** Run the tinyusb device task.  Must be called regularly from the main loop. */
void     drv_usb_cdc_task(void);

/** Queue bytes for transmission; returns the number accepted (0 if no host). */
uint32_t drv_usb_cdc_write(const void *data, uint32_t len);

/** Read up to len bytes; returns the number copied. */
uint32_t drv_usb_cdc_read(void *data, uint32_t len);

/** 1 when the port is mounted and the host has raised DTR. */
int      drv_usb_cdc_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_USB_CDC_H */
