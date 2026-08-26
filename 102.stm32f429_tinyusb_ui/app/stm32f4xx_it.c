/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  *          SVC/PendSV/SysTick are owned by FreeRTOS (see port.c);
  *          the HAL 1 ms tick uses TIM11 (stm32f4xx_hal_timebase_tim.c);
  *          OTG FS IRQ drives the TinyUSB host stack.
  ******************************************************************************
  */
#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "tusb.h"

/* The TIM11 time base handle is defined in stm32f4xx_hal_timebase_tim.c */
extern TIM_HandleTypeDef htim11;

/* FreeRTOS port exception handlers (defined in portable/GCC/ARM_CM4F/port.c) */
extern void vPortSVCHandler(void);
extern void xPortPendSVHandler(void);
extern void xPortSysTickHandler(void);

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
void NMI_Handler(void)
{
  while (1) { }
}

void HardFault_Handler(void)
{
  while (1) { }
}

void MemManage_Handler(void)
{
  while (1) { }
}

void BusFault_Handler(void)
{
  while (1) { }
}

void UsageFault_Handler(void)
{
  while (1) { }
}

/* SVC/PendSV/SysTick vectors point directly at the FreeRTOS port handlers
 * (see startup_stm32f429xx.s); nothing to define here. */

void DebugMon_Handler(void)
{
}

/**
  * @brief  TIM11 (mapped to TIM1_TRG_COM_TIM11_IRQn) = HAL 1 ms time base
  *         (SysTick belongs to FreeRTOS).
  */
void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim11);
}

/**
  * @brief  OTG FS global interrupt -> TinyUSB host stack.
  *         TinyUSB's OSAL uses FromISR primitives inside this handler, so the
  *         priority (set in bsp_usb_hw.c) is at configMAX_SYSCALL_INTERRUPT_PRIORITY.
  */
void OTG_FS_IRQHandler(void)
{
  tuh_int_handler(0);
}
