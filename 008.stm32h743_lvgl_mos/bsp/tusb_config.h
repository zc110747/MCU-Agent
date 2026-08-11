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
 *  Memory placement
 *
 *  The D-Cache is disabled board-wide (see MPU_Config in main.c), so USB
 *  buffers need no cache maintenance and can live in ordinary .bss on the AXI
 *  SRAM, which the OTG core can reach.
 * ------------------------------------------------------------------------*/
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
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
