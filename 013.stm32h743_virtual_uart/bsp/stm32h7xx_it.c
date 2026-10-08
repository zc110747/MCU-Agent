/* ---------------------------------------------------------------------------
 * Core / system interrupt handlers.
 *
 * Peripheral interrupts used by the software UART (TIM2/TIM3 update, DMA1
 * streams, EXTI lines) live next to the driver they belong to, in
 * bsp/sw_uart/.
 * -------------------------------------------------------------------------*/

#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "bsp.h"

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

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* Default_Handler is provided by the startup file (weak aliases). */
