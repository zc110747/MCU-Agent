/* TinyUSB configuration - STM32H743ZIT6, USB CDC (virtual COM port) device */
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

/* Both OTG cores are driven here through their internal full-speed PHY. */
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
   Output goes through board_uart_write / printf - keep at 0 for normal use. */
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

/*--------------------------------------------------------------------
 * Device stack
 *------------------------------------------------------------------*/
#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             1
#define CFG_TUD_MSC             1
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* MSC (USB Mass Storage / U-disk) - one LUN backed by the SD card. */
#define CFG_TUD_MSC_MAXLUN     1
#define CFG_TUD_MSC_EP_BUFSIZE 512   /* == SD block size, so offset is always 0 */

/* CDC FIFO sizes (bytes). 512 gives comfortable headroom for bulk echo. */
#define CFG_TUD_CDC_RX_BUFSIZE  512
#define CFG_TUD_CDC_TX_BUFSIZE  512

/* CDC bulk endpoint packet size - 64 is the max for full speed */
#define CFG_TUD_CDC_EP_BUFSIZE  64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
