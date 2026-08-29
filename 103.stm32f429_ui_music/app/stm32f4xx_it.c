/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  *          SVC/PendSV/SysTick are owned by FreeRTOS (see port.c);
  *          the HAL 1 ms tick uses TIM7 (stm32f4xx_hal_timebase_tim.c);
  *          OTG FS IRQ drives the TinyUSB host stack.
  ******************************************************************************
  */
#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "tusb.h"

/* The TIM7 time base handle is defined in stm32f4xx_hal_timebase_tim.c */
extern TIM_HandleTypeDef htim7;

/* Touch controller T_PEN (PH7) EXTI handle, defined in bsp/bsp_touch.c */
extern EXTI_HandleTypeDef g_touch_exti;

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
  * @brief  TIM7 (TIM7_IRQn = 55, dedicated vector) = HAL 1 ms time base
  *         (SysTick belongs to FreeRTOS).
  */
void TIM7_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim7);
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

/**
  * @brief  EXTI lines 9..5 -> the touch controller's T_PEN (PH7).
  *         HAL_EXTI_IRQHandler clears the pending bit and runs the callback
  *         registered by bsp_touch.c, which releases the touch semaphore
  *         (FromISR variant).  Priority 6 is below
  *         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5), so that is legal.
  */
void EXTI9_5_IRQHandler(void)
{
  HAL_EXTI_IRQHandler(&g_touch_exti);
}
