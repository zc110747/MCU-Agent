/* TinyUSB configuration - STM32H743ZIT6, USB Mass Storage (U-disk) device */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------------------------------------------------
 * Board / MCU
 *------------------------------------------------------------------*/
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU      OPT_MCU_STM32H7
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS       OPT_OS_NONE
#endif

/* rhport 0 = USB2_OTG_FS (PA11/PA12, internal FS PHY) */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

/*--------------------------------------------------------------------
 * Memory placement - DWC2 FIFO (slave) mode, buffers may sit anywhere.
 *------------------------------------------------------------------*/
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

/*--------------------------------------------------------------------
 * Device stack - MSC only (virtual U-disk backed by QSPI flash)
 *------------------------------------------------------------------*/
#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             0   /* no CDC - pure mass storage */
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* Single LUN, backed by the 8 MB QSPI flash. */
#define CFG_TUD_MSC_MAXLUN     1
#define CFG_TUD_MSC_EP_BUFSIZE 512   /* == flash logical block size */

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
