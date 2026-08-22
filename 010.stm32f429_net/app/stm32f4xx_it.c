/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  *          SVC/PendSV/SysTick are owned by FreeRTOS (see port.c);
  *          the HAL 1 ms tick uses TIM7 (stm32f4xx_hal_timebase_tim.c).
  ******************************************************************************
  */
#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "ethernetif.h"

/* The ETH handle is defined in ethernetif.c */
extern ETH_HandleTypeDef heth;

/* The TIM7 time base handle is defined in stm32f4xx_hal_timebase_tim.c */
extern TIM_HandleTypeDef htim7;

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
  * @brief  TIM7 = HAL 1 ms time base (SysTick belongs to FreeRTOS).
  */
void TIM7_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim7);
}

/**
  * @brief  This function handles Ethernet global interrupt.
  */
void ETH_IRQHandler(void)
{
  HAL_ETH_IRQHandler(&heth);
}
