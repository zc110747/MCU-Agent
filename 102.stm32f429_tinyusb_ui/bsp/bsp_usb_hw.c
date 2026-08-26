#include "bsp_usb_hw.h"
#include "FreeRTOSConfig.h"   /* for configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */

/**
  * @brief  Enable the OTG FS clock, configure PA11/PA12 as the FS USB data
  *         pair, and set the OTG FS interrupt priority.
  *
  *         TinyUSB's dwc2_stm32 port handles the controller/PHY init (GCCFG,
  *         turnaround, VBUS-sensing disable for host) inside tusb_init() ->
  *         hcd_init().  This function only brings up the silicon the port
  *         expects to already be clocked and pinned.
  *
  *         The OTG FS IRQ priority is set to
  *         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5) because TinyUSB's
  *         OSAL uses xSemaphoreGiveFromISR / xQueueSendToBackFromISR inside
  *         the ISR; that API is only legal at a priority that can call
  *         FromISR (i.e. numerically >= the FreeRTOS max-syscall priority).
  */
void USBH_HW_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* 1) OTG FS peripheral clock (F429: RCC_AHB2ENR.OTGFSEN) */
  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

  /* 2) Data pins PA11 (DM) / PA12 (DP) */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitStruct.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* 3) Interrupt: priority within the FreeRTOS-managed band, then enable.
   *    (tusb_init()/hcd_init() may also enable it; enabling twice is safe.) */
  HAL_NVIC_SetPriority(OTG_FS_IRQn,
                       configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}
