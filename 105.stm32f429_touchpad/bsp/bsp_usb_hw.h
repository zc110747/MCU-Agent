/**
 ******************************************************************************
 * @file    bsp_usb_hw.h
 * @brief   USB OTG FS board bring-up (clock, data pins, interrupt priority).
 *
 *          TinyUSB's dwc2_stm32 port programs the controller itself inside
 *          tusb_init(); this layer only enables the silicon it expects:
 *
 *              RCC      AHB2ENR.OTGFSEN
 *              GPIOA    PA11 (DM) / PA12 (DP), AF10, very high speed
 *              NVIC     OTG_FS_IRQn at configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
 *
 *          The interrupt priority matters: TinyUSB's FreeRTOS OSAL calls
 *          xSemaphoreGiveFromISR / xQueueSendToBackFromISR from the ISR, which
 *          is only legal at a priority that can reach the FromISR API.
 ******************************************************************************
 */
#ifndef __BSP_USB_HW_H
#define __BSP_USB_HW_H

#ifdef __cplusplus
extern "C" {
#endif

/** Clock + pins + NVIC.  Must run before usb_mouse_init(). */
void BSP_USB_HW_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_USB_HW_H */
