/**
 ******************************************************************************
 * @file    stm32f4xx_it.c
 * @brief   Interrupt service routines owned by this project.
 *
 *          SVC / PendSV / SysTick belong to the FreeRTOS port (their vectors
 *          point straight at vPortSVCHandler / xPortPendSVHandler /
 *          xPortSysTickHandler in sys_startup/gcc/startup_stm32f429xx.s).
 *
 *          TIM8_BRK_TIM12 -> HAL 1 ms time base (TIM12, pre-emption priority 0)
 *          DMA2_Stream3   -> SAI1_A TX (audio), see bsp/bsp_sai.c
 *          DMA2_Stream6   -> SDIO RX (video/audio file reads), see bsp/bsp_sdio.c
 *          SDIO           -> SDIO status/error collection, see bsp/bsp_sdio.c
 ******************************************************************************
 */
#include "stm32f4xx_it.h"
#include "stm32f4xx_hal.h"

#include "bsp_sai.h"
#include "bsp_sdio.h"

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
 * @brief  DMA2 Stream3 transfer complete -> SAI TX buffer consumed.
 *         The handler only advances the buffer index; refilling stays in the
 *         audio task, never in interrupt context.
 */
void DMA2_Stream3_IRQHandler(void)
{
    bsp_sai_dma_irq_handler();
}

/**
 * @brief  DMA2 Stream6 transfer complete -> one SD read burst finished.
 */
void DMA2_Stream6_IRQHandler(void)
{
    bsp_sdio_dma_rx_irq_handler();
}

/**
 * @brief  SDIO interrupt: closes the data path in DMA mode, collects errors.
 */
void SDIO_IRQHandler(void)
{
    bsp_sdio_irq_handler();
}
