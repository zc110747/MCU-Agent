/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt service routines.
  ******************************************************************************
  */
#include "main.h"
#include "stm32h7xx_it.h"
#include "bsp_console.h"

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

/**
  * @brief  USART1 (debug console, PA9/PA10).
  * @note   The HAL UART IRQ handler is deliberately bypassed: it only knows
  *         how to service a transfer started by HAL_UART_Receive_IT(), while
  *         this console wants a free-running RXNE that never has to be
  *         re-armed.  bsp_console_uart_irq() reads RDR and clears the error
  *         flags directly - see bsp_console.c.
  */
void USART1_IRQHandler(void)
{
    bsp_console_uart_irq();
}

/**
  * @brief  USB2 OTG FS (PA11/PA12) - the tinyusb device stack.
  * @note   The handler body lives in bsp/drv_usb_cdc.c next to the rest of the
  *         USB glue; it is declared weak in the startup file, so the strong
  *         definition there wins and nothing is needed here.
  */
