/* TinyUSB configuration - STM32H743ZIT6, CMSIS-DAP v1 (USB HID) probe */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------
 * Board / MCU
 *------------------------------------------------------------------*/
#ifndef CFG_TUSB_MCU
#error "CFG_TUSB_MCU must be defined by the build system"
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

/* rhport 0 = USB2_OTG_FS (PA11/PA12), rhport 1 = USB1_OTG_HS (PB14/PB15) */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

/* Both OTG cores are driven here through their internal full-speed PHY.
 * Full speed is not a limitation for CMSIS-DAP v1: the protocol is defined
 * around 64-byte HID reports, which is exactly the FS interrupt packet size. */
#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

/*--------------------------------------------------------------------
 * Memory placement
 *
 * The DWC2 core runs in FIFO (slave) mode - no bus-master DMA - so the
 * buffers may live in DTCM. Keep 4-byte alignment for FIFO copies.
 *------------------------------------------------------------------*/
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))

/* Debug level: 0 = off, 1 = errors, 2 = warnings, 3 = verbose.
 * Must stay 0 in normal use - printf inside the USB path wrecks DAP timing. */
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

/*--------------------------------------------------------------------
 * Device stack
 *------------------------------------------------------------------*/
#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             1
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* CMSIS-DAP v1 moves fixed 64-byte reports in both directions. This value is
 * the HID endpoint buffer size and must match DAP_PACKET_SIZE. */
#define CFG_TUD_HID_EP_BUFSIZE  64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
