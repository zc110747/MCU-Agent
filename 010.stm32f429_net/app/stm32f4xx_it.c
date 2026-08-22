/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */
#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"
#include "ethernetif.h"

/* The ETH handle is defined in ethernetif.c */
extern ETH_HandleTypeDef heth;

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

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick interrupts (1 ms time base).
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}

/**
  * @brief  This function handles Ethernet global interrupt.
  */
void ETH_IRQHandler(void)
{
  HAL_ETH_IRQHandler(&heth);
}
