/**
  ******************************************************************************
  * @file    stm32f4xx_hal_timebase_tim.c
  * @brief   HAL time base based on TIM7 (1 ms).
  *
  *          SysTick is owned by FreeRTOS, so the HAL 1 ms tick is moved to
  *          TIM7 (APB1 timer; APB1 prescaler is /4 so TIMCLK = 2 x 42 MHz =
  *          84 MHz, 16-bit timer).  Overrides the weak
  *          HAL_InitTick/HAL_SuspendTick/HAL_ResumeTick from stm32f4xx_hal.c.
  *          HAL_IncTick() is called from HAL_TIM_PeriodElapsedCallback
  *          (TIM7_IRQHandler).
  *
  *          HAL Tick (TIM7) and FreeRTOS Tick (SysTick) are INDEPENDENT:
  *          TIM7 drives HAL_Delay()/HAL_GetTick()/peripheral timeouts (SDIO
  *          included), while SysTick drives the RTOS scheduler.  They never
  *          conflict.
  *
  *          Why TIM7 and not TIM11: TIM7 has its own dedicated vector
  *          (TIM7_IRQn = 55), so the HAL tick no longer shares an IRQ line
  *          with TIM1 TRG/COM.  TIM11 is left completely free.
  ******************************************************************************
  */
#include "stm32f4xx_hal.h"

TIM_HandleTypeDef htim7;

/**
  * @brief  Configure TIM7 as the HAL time base source (1 ms tick).
  * @param  TickPriority: Tick interrupt priority.
  * @retval HAL status
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
  RCC_ClkInitTypeDef clkconfig;
  uint32_t uwTimclock = 0U;
  uint32_t uwPrescalerValue = 0U;
  uint32_t pFLatency;
  HAL_StatusTypeDef status;

  /* Enable TIM7 clock (APB1) */
  __HAL_RCC_TIM7_CLK_ENABLE();

  /* Get clock configuration */
  HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

  /* TIM7 is on APB1: timer clock = 2 x PCLK1 when the APB1 prescaler > 1.
   * With SYSCLK=168 MHz, APB1=/4 -> PCLK1=42 MHz, timer clock=84 MHz.
   * Before SystemClock_Config() runs (HSI 16 MHz, APB1 /1) the timer clock is
   * simply PCLK1; HAL_RCC_ClockConfig() re-invokes HAL_InitTick() once the
   * final clocks are up, so the prescaler is recomputed there. */
  if (clkconfig.APB1CLKDivider == RCC_HCLK_DIV1)
  {
    uwTimclock = HAL_RCC_GetPCLK1Freq();
  }
  else
  {
    uwTimclock = 2U * HAL_RCC_GetPCLK1Freq();
  }

  /* Prescaler to get a 1 MHz counter clock (84 MHz / 84 = 1 MHz) */
  uwPrescalerValue = (uint32_t)((uwTimclock / 1000000U) - 1U);

  /* TIM7: period = 1000 - 1 -> 1 ms tick */
  htim7.Instance = TIM7;
  htim7.Init.Period = (1000000U / 1000U) - 1U;
  htim7.Init.Prescaler = uwPrescalerValue;
  htim7.Init.ClockDivision = 0U;
  htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  htim7.Init.RepetitionCounter = 0U;

  status = HAL_TIM_Base_Init(&htim7);
  if (status == HAL_OK)
  {
    /* HAL_RCC_ClockConfig() re-calls HAL_InitTick() with the new clocks;
       by then htim7.State is BUSY from the first Start_IT and a second
       Start_IT would fail, silently killing the 1 ms time base.  Stop first
       to reset State=READY. */
    HAL_TIM_Base_Stop_IT(&htim7);
    status = HAL_TIM_Base_Start_IT(&htim7);
  }
  /* Enable the IRQ unconditionally: the tick must never be left dead.
   * TIM7 owns its own vector on the F4 (TIM7_IRQn). */
  HAL_NVIC_EnableIRQ(TIM7_IRQn);

  if ((status == HAL_OK) && (TickPriority < (1UL << __NVIC_PRIO_BITS)))
  {
    HAL_NVIC_SetPriority(TIM7_IRQn, TickPriority, 0);
    uwTickPrio = TickPriority;
  }
  else
  {
    status = HAL_ERROR;
  }

  return status;
}

/**
  * @brief  Suspend Tick increment (disable TIM7 update interrupt).
  */
void HAL_SuspendTick(void)
{
  __HAL_TIM_DISABLE_IT(&htim7, TIM_IT_UPDATE);
}

/**
  * @brief  Resume Tick increment (enable TIM7 update interrupt).
  */
void HAL_ResumeTick(void)
{
  __HAL_TIM_ENABLE_IT(&htim7, TIM_IT_UPDATE);
}

/**
  * @brief  Period elapsed callback -> HAL tick increment.
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM7)
  {
    HAL_IncTick();
  }
}
