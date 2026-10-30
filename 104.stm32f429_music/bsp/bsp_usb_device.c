/**
 ******************************************************************************
 * @file    bsp_usb_device.c
 * @brief   USB OTG FS device-role silicon bring-up (pins, clock, IRQ).
 ******************************************************************************
 */
#include "bsp_usb_device.h"

#include "FreeRTOSConfig.h" /* configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */
#include "stm32f4xx_hal.h"

void bsp_usb_device_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 1) OTG FS peripheral clock (F429: RCC_AHB2ENR.OTGFSEN).  48 MHz comes from
     *    PLLQ, which system_clock_config() already set up. */
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

    /* 2) PA11 = DM, PA12 = DP.  No pull-up: the OTG core drives the D+ pull-up
     *    itself when the device is connected (soft-connect in dcd_connect). */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* 3) The dwc2 ISR calls the FreeRTOS FromISR API (OSAL queue post), so its
     *    NVIC priority must be numerically >= configLIBRARY_MAX_SYSCALL_
     *    INTERRUPT_PRIORITY, exactly like the SAI DMA and USART3 interrupts. */
    HAL_NVIC_SetPriority(OTG_FS_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}
