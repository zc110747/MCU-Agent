/**
  ******************************************************************************
  * @file    stm32f4xx_hal_timebase_tim.c
  * @brief   HAL time base based on TIM11 (1 ms).
  *
  *          SysTick is owned by FreeRTOS, so the HAL 1 ms tick is moved to
  *          TIM11 (APB2 timer clock = 2 x 84 MHz = 168 MHz, 16-bit timer).
  *          Overrides the weak HAL_InitTick/HAL_SuspendTick/HAL_ResumeTick
  *          from stm32f4xx_hal.c.  HAL_IncTick() is called from
  *          HAL_TIM_PeriodElapsedCallback (TIM11_IRQHandler).
  *
  *          HAL Tick (TIM11) and FreeRTOS Tick (SysTick) are INDEPENDENT:
  *          TIM11 drives HAL_Delay()/HAL_GetTick()/peripheral timeouts, while
  *          SysTick drives the RTOS scheduler.  They never conflict.
  ******************************************************************************
  */
#include "stm32f4xx_hal.h"

TIM_HandleTypeDef htim11;

/**
  * @brief  Configure TIM11 as the HAL time base source (1 ms tick).
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

  /* Enable TIM11 clock (APB2) */
  __HAL_RCC_TIM11_CLK_ENABLE();

  /* Get clock configuration */
  HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

  /* TIM11 is on APB2: timer clock = 2 * PCLK2 when APB2 prescaler > 1.
   * With SYSCLK=168 MHz, APB2=/2 -> PCLK2=84 MHz, timer clock=168 MHz. */
  if (clkconfig.APB2CLKDivider == RCC_HCLK_DIV1)
  {
    uwTimclock = HAL_RCC_GetPCLK2Freq();
  }
  else
  {
    uwTimclock = 2 * HAL_RCC_GetPCLK2Freq();
  }

  /* Prescaler to get a 1 MHz counter clock (168 MHz / 168 = 1 MHz) */
  uwPrescalerValue = (uint32_t)((uwTimclock / 1000000U) - 1U);

  /* TIM11: period = 1000 - 1 -> 1 ms tick */
  htim11.Instance = TIM11;
  htim11.Init.Period = (1000000U / 1000U) - 1U;
  htim11.Init.Prescaler = uwPrescalerValue;
  htim11.Init.ClockDivision = 0U;
  htim11.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim11.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  status = HAL_TIM_Base_Init(&htim11);
  if (status == HAL_OK)
  {
    /* HAL_RCC_ClockConfig() re-calls HAL_InitTick() with the new clocks;
       by then htim11.State is BUSY from the first Start_IT and a second
       Start_IT would fail, silently killing the 1 ms time base.  Stop first
       to reset State=READY. */
    HAL_TIM_Base_Stop_IT(&htim11);
    status = HAL_TIM_Base_Start_IT(&htim11);
  }
  /* Enable the IRQ unconditionally: the tick must never be left dead.
   * On STM32F4 TIM11 shares TIM1_TRG_COM_TIM11_IRQn. */
  HAL_NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn);

  if ((status == HAL_OK) && (TickPriority < (1UL << __NVIC_PRIO_BITS)))
  {
    HAL_NVIC_SetPriority(TIM1_TRG_COM_TIM11_IRQn, TickPriority, 0);
    uwTickPrio = TickPriority;
  }
  else
  {
    status = HAL_ERROR;
  }

  return status;
}

/**
  * @brief  Suspend Tick increment (disable TIM11 update interrupt).
  */
void HAL_SuspendTick(void)
{
  __HAL_TIM_DISABLE_IT(&htim11, TIM_IT_UPDATE);
}

/**
  * @brief  Resume Tick increment (enable TIM11 update interrupt).
  */
void HAL_ResumeTick(void)
{
  __HAL_TIM_ENABLE_IT(&htim11, TIM_IT_UPDATE);
}

/**
  * @brief  Period elapsed callback -> HAL tick increment.
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM11)
  {
    HAL_IncTick();
  }
}
