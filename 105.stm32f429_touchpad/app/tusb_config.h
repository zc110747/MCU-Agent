/**
 ******************************************************************************
 * @file    tusb_config.h
 * @brief   TinyUSB configuration: OTG FS (port 0) as a HID mouse device.
 *
 *  CFG_TUSB_MCU / CFG_TUSB_OS are also supplied on the command line by
 *  CMakeLists.txt; the #ifndef guards below keep a bare compile (IDE indexer,
 *  unit build) working as well.
 *
 *  Only the device role and the HID class are enabled.  Host, MSC and CDC stay
 *  off so the image keeps nothing it does not use.
 ******************************************************************************
 */
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU              OPT_MCU_STM32F4
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS               OPT_OS_FREERTOS
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG            0
#endif

/* ---- Port 0 = USB OTG FS, device role, full speed ------------------------ */
#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

/* The board is bus/ST-Link powered and PA9 (VBUS sense) is not wired to a
 * divider, so the controller must be told not to wait for a VBUS session:
 * without this the DWC2 core keeps B-session invalid and never enumerates. */
#ifndef CFG_TUD_VBUS_DETECT_HW
#define CFG_TUD_VBUS_DETECT_HW    0
#endif

/* RAM is plentiful (SDRAM heap); keep the TinyUSB defaults simple. */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN        __attribute__ ((aligned(4)))
#endif

/* ---- Device -------------------------------------------------------------- */
#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

/* 1 = HID device class compiled in (usb_descriptors.c + usb_mouse.c). */
#define CFG_TUD_HID               1
#define CFG_TUD_HID_EP_BUFSIZE    16

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H_ */
