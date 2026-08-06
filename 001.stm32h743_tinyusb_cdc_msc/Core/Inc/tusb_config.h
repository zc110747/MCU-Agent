/**
 ******************************************************************************
 * @file    tusb_config.h
 * @brief   TinyUSB configuration for STM32H743 USB OTG FS UVC device.
 ******************************************************************************
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Board / controller
 * -------------------------------------------------------------------------- */
#ifndef CFG_TUSB_MCU
#error "CFG_TUSB_MCU must be defined (see CMakeLists.txt)"
#endif

#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT        0            /* rhport 0 == USB_OTG_FS on STM32 */
#endif

#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED     OPT_MODE_FULL_SPEED
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS             OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG          0
#endif

/* --------------------------------------------------------------------------
 * Device stack
 * -------------------------------------------------------------------------- */
#define CFG_TUD_ENABLED         1
#define CFG_TUD_MAX_SPEED       BOARD_TUD_MAX_SPEED

/* The FS core has no internal DMA worth using here. Slave (FIFO) mode keeps
 * the CPU in charge of every byte, which sidesteps all D-cache coherency
 * questions between the USB core and our AXI-SRAM frame buffers. */
#define CFG_TUD_DWC2_SLAVE_ENABLE   1
#define CFG_TUD_DWC2_DMA_ENABLE     0
#define CFG_TUD_MEM_DCACHE_ENABLE   0

/* USB endpoint/DMA visible buffers stay in ordinary .bss (DTCM). */
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN      __attribute__((aligned(4)))
#endif

#define CFG_TUD_ENDPOINT0_SIZE  64

/* --------------------------------------------------------------------------
 * Classes: video only
 * -------------------------------------------------------------------------- */
#define CFG_TUD_VIDEO               1   /* number of video control interfaces   */
#define CFG_TUD_VIDEO_STREAMING     1   /* number of video streaming interfaces */

/* Isochronous, one transaction per 1 ms frame.
 *
 * USB allows up to 1023 bytes per FS isochronous packet, but on this chip we
 * are limited by the USB2_OTG_FS packet SPRAM, which is only 1.25 KB
 * (320 x 32-bit words). TinyUSB carves it up like this:
 *
 *   GRXFSIZ   = 13 + 1 + 2*(64/4 + 1) + 2*9  =  66 words   (shared RX)
 *   EP0 IN TX = 64/4                         =  16 words
 *   ------------------------------------------------------
 *   left for the isochronous IN endpoint     = 238 words = 952 bytes
 *
 * Each packet spends 2 bytes on the UVC payload header, so a frame needs
 *   ceil(115200 / 950) = 122 packets -> ~122 ms -> ~8 fps,
 * which is essentially the same as the theoretical 1023-byte maximum.
 */
#define CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE  952
#define CFG_TUD_VIDEO_STREAMING_BULK        0

/* Fail loudly at compile time rather than silently corrupting USB traffic if
 * the endpoint size is ever raised past what the FS packet RAM can hold. */
#define UVC_FS_DFIFO_WORDS    320   /* USB2_OTG_FS SPRAM = 1.25 KB */
#define UVC_FS_GRXFSIZ_WORDS   66
#define UVC_FS_EP0TX_WORDS     16
#if (((CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE + 3) / 4) \
     + UVC_FS_GRXFSIZ_WORDS + UVC_FS_EP0TX_WORDS) > UVC_FS_DFIFO_WORDS
#error "CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE exceeds the USB2_OTG_FS packet RAM budget"
#endif

#define CFG_TUD_CDC             0
#define CFG_TUD_MSC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_AUDIO           0
#define CFG_TUD_VENDOR          0
#define CFG_TUD_DFU             0
#define CFG_TUD_ECM_RNDIS       0
#define CFG_TUD_NCM             0

/* Host stack disabled */
#define CFG_TUH_ENABLED         0

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
