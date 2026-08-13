/**
  ******************************************************************************
  * @file    tusb_config.h
  * @brief   tinyusb configuration for the STM32H743 USB-FS device port.
  *
  *  The board wires the USB-C connector to PA11/PA12, which on the H743 is the
  *  *second* OTG core (USB2_OTG_FS).  tinyusb calls that rhport 0 (see the
  *  controller table in dwc2_stm32.h), so BOARD_TUD_RHPORT stays 0.
  *
  *  VBUS sensing is switched off: the VBUS pin (PA9) is taken by USART1 on
  *  this board, so the core would never see a valid VBUS and would refuse to
  *  pull D+ up.  With CFG_TUD_VBUS_DETECT_HW = 0 the device attaches as soon
  *  as the stack is initialised.
  ******************************************************************************
  */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 *  Board / MCU
 * ------------------------------------------------------------------------*/
#define CFG_TUSB_MCU                OPT_MCU_STM32H7
#define CFG_TUSB_OS                 OPT_OS_NONE
#define CFG_TUSB_DEBUG              0

#define BOARD_TUD_RHPORT            0
#define BOARD_TUD_MAX_SPEED         OPT_MODE_FULL_SPEED

#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED             1
#define CFG_TUH_ENABLED             0
#define CFG_TUD_MAX_SPEED           BOARD_TUD_MAX_SPEED

/* No VBUS pin available - attach unconditionally. */
#define CFG_TUD_VBUS_DETECT_HW      0

/* --------------------------------------------------------------------------
 *  Memory placement & D-Cache coherency
 *
 *  D-Cache IS enabled board-wide (see MPU_Config in main.c).  The OTG internal
 *  DMA reads/writes the USB buffers (DWC2 descriptors, CDC RX/TX data) directly,
 *  so the CPU and DMA must agree on their contents or the console dies after a
 *  few minutes of traffic (classic write-back coherency corruption -> BusFault).
 *
 *  Two complementary measures keep every DMA-touched buffer coherent:
 *
 *   1. CFG_TUD_MEM_DCACHE_ENABLE = 1  (the authoritative fix)
 *      tinyusb calls dcd_dcache_clean()/invalidate() around EVERY xfer->buffer
 *      (the real CDC RX/TX data buffers and the EP0 buffer) - see dcd_dwc2.c
 *      lines 392 / 1015 / 1039 / 1064.  This covers the buffers that live in
 *      ordinary .bss (cdcd_interface_t.rx_ff_buf / tx_ff_buf) which the
 *      CFG_TUSB_MEM_SECTION trick alone does NOT reach.
 *
 *   2. CFG_TUSB_MEM_SECTION -> .usb_ram (defence in depth)
 *      Routes the DWC2 DMA descriptors (_ctrl_epbuf, _dcd_usbbuf) into SRAM4
 *      (D3, 0x38000000); MPU Region 2 marks that region non-cacheable, so the
 *      descriptors never need manual maintenance.
 *
 *  Together they cover all DMA-accessed memory on the H7 USB-FS device port.
 * ------------------------------------------------------------------------*/
#ifndef CFG_TUD_MEM_DCACHE_ENABLE
#define CFG_TUD_MEM_DCACHE_ENABLE   1
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION       __attribute__ ((section(".usb_ram"), aligned(4)))
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__ ((aligned(4)))
#endif

/* --------------------------------------------------------------------------
 *  Device stack
 * ------------------------------------------------------------------------*/
#define CFG_TUD_ENDPOINT0_SIZE      64

#define CFG_TUD_CDC                 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/* A whole NES frame of key traffic is tiny; the TX side is the one that
 * matters because log lines are pushed from a blocking context. */
#define CFG_TUD_CDC_RX_BUFSIZE      512
#define CFG_TUD_CDC_TX_BUFSIZE      1024
#define CFG_TUD_CDC_EP_BUFSIZE      64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
