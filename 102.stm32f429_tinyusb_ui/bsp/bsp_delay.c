
#include "bsp_delay.h"

void bsp_delay_us(uint32_t us)
{
  /* Enable the DWT cycle counter (needs TRCENA in CoreDebug). */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  DWT->CYCCNT = 0;

  uint32_t target = us * (SystemCoreClock / 1000000U);
  uint32_t start  = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < target)
  {
    /* busy wait */
  }
}

__weak void bsp_delay_ms(uint32_t ms)
{
  bsp_delay_us(ms * 1000U);
}