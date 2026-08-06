/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt service routines.
  ******************************************************************************
  */
#include "main.h"
#include "stm32h7xx_it.h"

/******************************************************************************/
/*           Cortex-M7 Processor Interruption and Exception Handlers          */
/******************************************************************************/

void NMI_Handler(void)
{
    /* The HSE Clock Security System routes crystal failure to NMI.
     * This clears the CSS flag and invokes HAL_RCC_CSSCallback() in main.c,
     * which latches the fault so the main loop can report it.
     * Hardware has already switched SYSCLK from the dead crystal to HSI, so
     * execution can safely continue instead of hanging here. */
    HAL_RCC_NMI_IRQHandler();
}

void HardFault_Handler(void)
{
    while (1)
    {
    }
}

void MemManage_Handler(void)
{
    while (1)
    {
    }
}

void BusFault_Handler(void)
{
    while (1)
    {
    }
}

void UsageFault_Handler(void)
{
    while (1)
    {
    }
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
  * @brief  1 ms system tick, drives HAL_Delay() / HAL_GetTick().
  */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/******************************************************************************/
/*                 STM32H7xx Peripheral Interrupt Handlers                    */
/******************************************************************************/

/**
  * @brief  SDMMC1 global interrupt.
  * @note   Required by the HAL SD driver: on STM32H7 HAL_SD_ReadBlocks()/
  *         HAL_SD_WriteBlocks() use the SDMMC internal DMA and rely on this
  *         IRQ to complete the transfer bookkeeping.
  */
void SDMMC1_IRQHandler(void)
{
    HAL_SD_IRQHandler(&hsd1);
}
