/* ---------------------------------------------------------------------------
 * TinyUSB configuration - STM32H743ZIT6, single HID mouse, no RTOS.
 * -------------------------------------------------------------------------*/
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

/* rhport 0 = USB2_OTG_FS on PA11/PA12. */
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

#define BOARD_TUD_MAX_SPEED   OPT_MODE_FULL_SPEED
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED       1
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

/* The DWC2 core runs in FIFO (slave) mode - no bus-master DMA - so the USB
 * buffers may live in DTCM, which keeps the DMA-capable AXI/D2 SRAM free for
 * the UART and DCMI buffers. */
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))

/* 0 = off. Do NOT raise this here: with a single HID interface there is no
 * CDC port to print to, and the log would go to the debugger only. */
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

/* One full-speed interrupt endpoint. 5 bytes of payload (see
 * mouse_report.h) plus the report ID; 16 leaves room for a future descriptor
 * extension without touching the endpoint size the host negotiated. */
#define CFG_TUD_HID_EP_BUFSIZE  16

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
