/* USB board support for STM32H743ZIT6 (OTG_FS PA11/PA12). */
#ifndef __USB_BOARD_H
#define __USB_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Enable the USB2_OTG_FS clock, pins, CRS trim, and NVIC. Call before tusb_init(). */
void BSP_USB_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_BOARD_H */
