/* TinyUSB configuration - STM32H743ZIT6, single CDC-ACM device, no RTOS */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error "CFG_TUSB_MCU must be defined by the build system"
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

/* rhport 0 = USB2_OTG_FS on PA11/PA12.
 * rhport 1 (USB1_OTG_HS) is NOT usable here: its DM pin PB14 is wired to
 * UART4_RTS on this board. */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

/* The DWC2 core runs in FIFO (slave) mode - no bus-master DMA - so the USB
 * buffers may live in DTCM, which keeps the DMA-capable AXI SRAM free for the
 * UART DMA buffers. */
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))

/* 0 = off. Raise to 2/3 only when debugging the stack itself; the log goes
 * through the CDC port, which is the very thing under test here. */
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

/*--------------------------------------------------------------------
 * Device stack
 *------------------------------------------------------------------*/
#define CFG_TUD_ENDPOINT0_SIZE  64

#define CFG_TUD_CDC             1
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          0

/* CDC FIFOs. 1 KB each: enough to keep a full-speed bulk pipe busy, small
 * enough that a short packet is never delayed waiting for more data. */
#define CFG_TUD_CDC_RX_BUFSIZE  1024
#define CFG_TUD_CDC_TX_BUFSIZE  1024

/* 64 bytes is the maximum bulk packet size at full speed */
#define CFG_TUD_CDC_EP_BUFSIZE  64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
