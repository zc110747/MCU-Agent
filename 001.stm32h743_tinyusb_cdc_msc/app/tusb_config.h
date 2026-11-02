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
 * buffers may live anywhere the CPU can reach. Everything the class
 * drivers allocate through this attribute goes to AXI SRAM (RAM_D1),
 * because 32 KB class buffers plus the 128 KB SD cache do not fit in the
 * 128 KB DTCM. Only the CDC FIFOs stay implicit (.bss -> DTCM), since
 * TinyUSB declares them without the attribute.
 *------------------------------------------------------------------*/
#define CFG_TUSB_MEM_SECTION  __attribute__((section(".ram_d1")))
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
#define CFG_TUD_MSC_EP_BUFSIZE 32768 /* 64 SD blocks per USB transfer. The link
                                        is Full Speed (12 Mbit/s, ~1.2 MB/s raw),
                                        so per-transfer fixed costs dominate:
                                        every extra block in one BOT round trip
                                        amortises the card's write-programming
                                        latency. Measured on this board,
                                        512 B -> 147 KB/s, 4 KB -> 619 KB/s,
                                        32 KB -> 921 KB/s write (read saturates
                                        at ~1.01 MB/s from 4 KB upwards).
                                        Must stay a multiple of the 512 B block
                                        size so transfers stay sector aligned,
                                        and below UINT16_MAX (TinyUSB asserts). */

/* CDC FIFO sizes (bytes). 32 KB each way: a host that stalls its reader for
 * a moment, or a burst of `flood` lines, is absorbed here instead of being
 * dropped by the application. 0x8000 is the hard ceiling of tu_fifo. These
 * two land in .bss (DTCM) - TinyUSB does not put the CDC FIFOs in the MEM
 * section - so keep an eye on the DTCM budget in the size report. */
#define CFG_TUD_CDC_RX_BUFSIZE  32768
#define CFG_TUD_CDC_TX_BUFSIZE  32768

/* CDC bulk endpoint packet size - 64 is the max for full speed */
#define CFG_TUD_CDC_EP_BUFSIZE  64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
