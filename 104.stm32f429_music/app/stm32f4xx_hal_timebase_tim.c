/**
 ******************************************************************************
 * @file    stm32f4xx_hal_timebase_tim.c
 * @brief   HAL time base on TIM12 (1 ms), highest NVIC priority.
 *
 *          SysTick belongs to FreeRTOS, so HAL_GetTick()/HAL_Delay() and every
 *          HAL peripheral timeout (SDIO polled transfers included) are driven
 *          by TIM12 instead.
 *
 *          Priority
 *          --------
 *          TIM12 runs at pre-emption priority 0 (highest).  That is deliberate:
 *          HAL_Delay() must stay functional inside taskENTER_CRITICAL() and in
 *          HAL init code that runs with BASEPRI raised.  The ISR only calls
 *          HAL_IncTick(); it never calls a FreeRTOS API, so being above
 *          configMAX_SYSCALL_INTERRUPT_PRIORITY is legal.  Do NOT add a
 *          FromISR call here without lowering the priority to 6.
 *
 *          Clock
 *          -----
 *          TIM12 is an APB1 timer.  With APB1 prescaler > 1 the timer clock is
 *          2 x PCLK1 (84 MHz at SYSCLK=168 MHz), so PSC gives a 1 MHz counter
 *          and ARR = 999 gives the 1 ms period.  HAL_RCC_ClockConfig() calls
 *          HAL_InitTick() again once the final clocks are up, so the prescaler
 *          is recomputed there.
 *
 *          Why TIM12 owns a free vector: on the F4 it is grouped with the TIM8
 *          break interrupt (TIM8_BRK_TIM12_IRQn = 43).  TIM8 is unused in this
 *          project, so the shared vector belongs to TIM12 alone.
 ******************************************************************************
 */
#include "stm32f4xx_hal.h"

TIM_HandleTypeDef htim12;

/**
 * @brief  Configure TIM12 as the HAL time base (1 ms tick).
 * @param  TickPriority: requested tick interrupt priority (0 by default).
 * @retval HAL status
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    RCC_ClkInitTypeDef clkconfig;
    uint32_t           uwTimclock       = 0U;
    uint32_t           uwPrescalerValue = 0U;
    uint32_t           pFLatency;
    HAL_StatusTypeDef  status;

    __HAL_RCC_TIM12_CLK_ENABLE();

    HAL_RCC_GetClockConfig(&clkconfig, &pFLatency);

    if (clkconfig.APB1CLKDivider == RCC_HCLK_DIV1)
    {
        uwTimclock = HAL_RCC_GetPCLK1Freq();
    }
    else
    {
        uwTimclock = 2U * HAL_RCC_GetPCLK1Freq();
    }

    /* 1 MHz counter clock */
    uwPrescalerValue = (uint32_t)((uwTimclock / 1000000U) - 1U);

    htim12.Instance               = TIM12;
    htim12.Init.Period            = (1000000U / 1000U) - 1U;
    htim12.Init.Prescaler         = uwPrescalerValue;
    htim12.Init.ClockDivision     = 0U;
    htim12.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    htim12.Init.RepetitionCounter = 0U;

    status = HAL_TIM_Base_Init(&htim12);
    if (status == HAL_OK)
    {
        /* HAL_RCC_ClockConfig() re-calls HAL_InitTick() with the new clocks; by
           then htim12.State is BUSY from the first Start_IT, so a second Start_IT
           would fail and silently kill the 1 ms time base.  Stop first. */
        HAL_TIM_Base_Stop_IT(&htim12);
        status = HAL_TIM_Base_Start_IT(&htim12);
    }

    /* The tick must never be left dead: enable the vector unconditionally. */
    HAL_NVIC_EnableIRQ(TIM8_BRK_TIM12_IRQn);

    if ((status == HAL_OK) && (TickPriority < (1UL << __NVIC_PRIO_BITS)))
    {
        HAL_NVIC_SetPriority(TIM8_BRK_TIM12_IRQn, TickPriority, 0);
        uwTickPrio = TickPriority;
    }
    else
    {
        status = HAL_ERROR;
    }

    return status;
}

/**
 * @brief  Suspend the tick increment (TIM12 update interrupt off).
 */
void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&htim12, TIM_IT_UPDATE);
}

/**
 * @brief  Resume the tick increment.
 */
void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&htim12, TIM_IT_UPDATE);
}

/**
 * @brief  Period elapsed callback -> HAL tick increment.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM12)
    {
        HAL_IncTick();
    }
}
