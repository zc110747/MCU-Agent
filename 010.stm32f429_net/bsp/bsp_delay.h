/**
  ******************************************************************************
  * @file    bsp_delay.h
  * @brief   DWT cycle-counter busy-wait delay (HAL_Delay / vTaskDelay free).
  *
  * Used only for pre-scheduler init sequences (PCF8574 reset, SDRAM) where
  * vTaskDelay() is illegal (scheduler not running) and HAL_Delay() depends on
  * the TIM7 interrupt which FreeRTOS may mask (BASEPRI).  A DWT busy-wait is
  * pure CPU counting: it never blocks on an interrupt, so it is safe both
  * before and after vTaskStartScheduler().
  *
  * Tasks that are already running MUST use vTaskDelay() instead of this
  * (busy-wait would burn CPU on the scheduler's time slice).
  ******************************************************************************
  */
#ifndef __BSP_DELAY_H__
#define __BSP_DELAY_H__

#include "stm32f4xx_hal.h"

static inline void bsp_delay_us(uint32_t us)
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

static inline void bsp_delay_ms(uint32_t ms)
{
  bsp_delay_us(ms * 1000U);
}

#endif /* __BSP_DELAY_H__ */
