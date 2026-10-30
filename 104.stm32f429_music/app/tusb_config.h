/*--------------------------------------------------------------------
 * TinyUSB configuration: STM32F429IGT6 USB OTG FS **device**, mass storage
 * class, backing store = the microSD card.
 *
 * The device role is what makes the board show up as a U-disk on a PC.  The
 * host role is not used in this project (see 102.stm32f429_tinyusb_ui for the
 * USB-host variant), so CFG_TUH_ENABLED stays 0 and only the device sources are
 * compiled.
 *--------------------------------------------------------------------*/
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Supplied here instead of on the command line so the GCC and MDK-ARM builds
 * cannot drift apart: both simply include this file. */
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_STM32F4
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_FREERTOS
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

/*--------------------------------------------------------------------
 * Device configuration
 *------------------------------------------------------------------*/
/* OTG FS only: the F429 OTG_FS core has an on-chip full-speed PHY and no HS
 * PHY, so FULL_SPEED is the only honest setting (12 Mbit/s). */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED

#define CFG_TUD_MSC 1
#define CFG_TUD_CDC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

/* One LUN (the SD card), and the SCSI layer buffer.
 *
 * The buffer must be a multiple of the 512-byte block size: msc_device.c calls
 * tud_msc_read10_cb()/write10_cb() with `lba + (xferred / block_size)` and
 * `offset = xferred % block_size`, so a multiple-of-512 buffer keeps every call
 * block aligned and lets us hand the SDIO driver the whole burst in one go.
 * 4 KB is also roughly the peak throughput of a polled 12 MHz 4-bit card. */
#define CFG_TUD_MSC_EP_BUFSIZE 4096

/*--------------------------------------------------------------------
 * Host configuration: not used
 *------------------------------------------------------------------*/
#define CFG_TUH_ENABLED 0

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
