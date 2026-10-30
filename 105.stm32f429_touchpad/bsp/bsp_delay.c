/**
 ******************************************************************************
 * @file    bsp_delay.c
 * @brief   Microsecond busy wait built on the Cortex-M4 DWT cycle counter.
 *
 *  The counter is started once and then left alone.  It used to be reset on
 *  every call, which looked harmless but broke every consumer that measures a
 *  duration by subtracting two CYCCNT samples: the bit-banged I2C delay calls
 *  this function several times per byte, so a scan appeared to take 12.9 ms
 *  instead of ~1.2 ms.  Starting it once also removes four register writes from
 *  the innermost loop of the touch driver.
 ******************************************************************************
 */
#include "bsp_delay.h"

void bsp_delay_us(uint32_t us)
{
    static uint8_t s_dwt_started = 0U;
    uint32_t target;
    uint32_t start;

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
