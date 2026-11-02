/**
 * @file    bsp_delay.c
 * @brief   Microsecond busy wait built on the Cortex-M4 DWT cycle counter.
 */
#include "bsp_delay.h"

void bsp_delay_us(uint32_t us)
{
    static uint8_t s_dwt_started = 0U;
    uint32_t       target;
    uint32_t       start;

    if (s_dwt_started == 0U)
    {
        /* TRCENA must be set before CYCCNT counts anything. */
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        s_dwt_started = 1U;
    }

    target = us * (SystemCoreClock / 1000000U);
    start  = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < target)
    {
        /* busy wait */
    }
}

__weak void bsp_delay_ms(uint32_t ms)
{
    bsp_delay_us(ms * 1000U);
}
