/**
  ******************************************************************************
  * @file    app/stm32h7xx_it.c
  * @brief   Interrupt handlers for the QSPI test application
  ******************************************************************************
  */
#include "stm32h7xx_hal.h"
#include "tusb.h"

/* SysTick is used by the HAL timebase (HAL_Delay / HAL_GetTick). Without a
   real handler the IRQ would fall through to Default_Handler (infinite loop)
   and freeze the whole program. */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* USB OTG_FS interrupt -> TinyUSB device stack (rhport 0). */
void OTG_FS_IRQHandler(void)
{
    tud_int_handler(0);
}
