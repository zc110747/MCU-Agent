/**
 ******************************************************************************
 * @file    bsp_usb_hw.c
 * @brief   USB OTG FS clock / pin / NVIC bring-up (device role).
 *
 *  Wiring on the ALIENTEK Apollo F429 board (same as 102):
 *      PA11 = USB_OTG_FS_DM
 *      PA12 = USB_OTG_FS_DP
 *
 *  VBUS sensing is deliberately left to TinyUSB: CFG_TUD_VBUS_DETECT_HW is 0 in
 *  app/tusb_config.h because PA9 is not wired to a VBUS divider on this board.
 ******************************************************************************
 */
#include "bsp_usb_hw.h"

#include "stm32f4xx_hal.h"
#include "FreeRTOSConfig.h"     /* configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */

void BSP_USB_HW_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 1) OTG FS peripheral clock (F429: RCC_AHB2ENR.OTGFSEN). */
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

    /* 2) Data pins PA11 (DM) / PA12 (DP) as AF10. */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 3) Interrupt inside the FreeRTOS-managed priority band. */
    HAL_NVIC_SetPriority(OTG_FS_IRQn,
                         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}
