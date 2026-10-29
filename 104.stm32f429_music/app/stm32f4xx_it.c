/**
 ******************************************************************************
 * @file    stm32f4xx_it.c
 * @brief   Interrupt Service Routines.
 *
 *          SVC / PendSV / SysTick belong to the FreeRTOS port (their vectors
 *          point straight at vPortSVCHandler / xPortPendSVHandler /
 *          xPortSysTickHandler, see FreeRTOSConfig.h and the startup file).
 *
 *          Every IRQ this project owns is routed through a bsp_* entry point so
 *          that no driver state has to be visible here.
 ******************************************************************************
 */
#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"

#include "bsp_sai.h"

/* TIM12 time base handle, defined in stm32f4xx_hal_timebase_tim.c */
extern TIM_HandleTypeDef htim12;

void NMI_Handler(void)
{
    while (1)
    {
    }
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

/* SVC / PendSV / SysTick are provided by the FreeRTOS port. */
void DebugMon_Handler(void)
{
}

/**
 * @brief  TIM12 update -> HAL 1 ms time base.
 *         Shares vector 43 with the (unused) TIM8 break interrupt.
 */
void TIM8_BRK_TIM12_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim12);
}

/**
 * @brief  DMA2 Stream3 transfer complete -> SAI TX buffer swapped.
 *         The handler only releases a semaphore; all refilling happens in the
 *         audio task, never in interrupt context.
 */
void DMA2_Stream3_IRQHandler(void)
{
    bsp_sai_dma_irq_handler();
}
