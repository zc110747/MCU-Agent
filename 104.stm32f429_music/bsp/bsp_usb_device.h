/**
 ******************************************************************************
 * @file    bsp_usb_device.h
 * @brief   USB OTG FS device-role silicon bring-up (pins, clock, IRQ).
 *
 *  Only the parts TinyUSB's dwc2 driver expects to find already configured are
 *  handled here: the OTG FS clock, the PA11/PA12 alternate function and the NVIC
 *  entry.  Core/PHY initialisation (GCCFG power-down, turnaround, forced B
 *  session when VBus sensing is unavailable) is done by TinyUSB inside
 *  tusb_init().
 ******************************************************************************
 */
#ifndef BSP_USB_DEVICE_H
#define BSP_USB_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Enable the OTG FS clock, mux PA11 (DM) / PA12 (DP) and enable the
 *         OTG_FS interrupt at the priority FreeRTOS's FromISR API requires.
 *
 *  Pin map (STM32F429IGT6, AF10)
 *  ----------------------------
 *    PA11 -> USB_OTG_FS_DM
 *    PA12 -> USB_OTG_FS_DP
 *
 *  These pins are free on this board: FMC (SDRAM + LCD) claims PC0/PC2/PC3,
 *  PD/PE/PF/PG only, and SDIO claims PC8..PC12 + PD2.
 *
 *  Must be called before tusb_init().
 */
void bsp_usb_device_init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USB_DEVICE_H */
