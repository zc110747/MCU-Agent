/**
 ******************************************************************************
 * @file    usb_msc.h
 * @brief   USB mass-storage device: exposes the microSD card as a U-disk.
 ******************************************************************************
 */
#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    USB_MSC_STATE_IDLE = 0, /* no host, the player owns the card */
    USB_MSC_STATE_ACTIVE,   /* host enumerated us: the card belongs to the PC */
} USB_MSC_STATE_T;

/**
 * @brief  Bring up the OTG FS device and the TinyUSB device stack.
 *
 *  Ordering: after vPortDefineHeapRegions() (the OSAL queue comes from the
 *  FreeRTOS heap) and after the SD card has been mounted at least once, because
 *  the first hand-over reports the card geometry.
 */
void usb_msc_init(void);

/** Create the USB task that pumps the device stack.  Call with the other tasks. */
void usb_msc_start(void);

/**
 * @brief  Transfer-screen state, polled by the UI task.
 * @retval USB_MSC_STATE_ACTIVE from the moment the host has enumerated us (and
 *         the SD card has been handed over) until the cable is pulled again.
 */
USB_MSC_STATE_T usb_msc_state(void);

/**
 * @brief  Card capacity snapshot taken when the transfer screen came up.
 *         Deliberately NOT updated while the session runs: the FAT belongs to
 *         the host, so reading it would be both wrong and a bus conflict.
 */
void usb_msc_capacity_mb(uint32_t *total_mb, uint32_t *used_mb);

/**
 * @brief  Print one line of hand-over state: state / card ownership / bus
 *         presence / SCSI command count / time since the last SCSI command.
 *         Used while the page switch is disabled to see whether the state
 *         machine itself is behaving.
 */
void usb_msc_debug_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_MSC_H */
