#ifndef __BSP_USB_HW_H
#define __BSP_USB_HW_H

#include "stm32f4xx_hal.h"

/* USB OTG FS Host on STM32F429IGT6.
 *   PA11 = OTG_FS_DM, PA12 = OTG_FS_DP  (AF10, internal FS PHY)
 *   Clock: dedicated 48 MHz from PLL48CLK (PLLQ=7 @168 MHz SYSCLK).
 *
 * TinyUSB's dwc2_stm32 port disables VBUS sensing automatically when the
 * controller is configured as host, so no VBUS pin handling is required. */

void USBH_HW_Init(void);

#endif /* __BSP_USB_HW_H */
