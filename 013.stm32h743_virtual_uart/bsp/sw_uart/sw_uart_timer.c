/* ---------------------------------------------------------------------------
 * Software UART - bit-timing timer abstraction.
 * -------------------------------------------------------------------------*/

#include "sw_uart_timer.h"
#include "stm32h7xx_hal.h"

/* Resolved timing for the configured baud rate; the single source of truth
 * reported back to the application. */
static sw_uart_timer_info_t g_timer_info = {0};

/* Persistent handles: the __HAL_TIM_* control macros dereference ->Instance,
 * so the timers cannot be driven from a bare TIM_TypeDef. */
static TIM_HandleTypeDef g_tx_timer = {0};
static TIM_HandleTypeDef g_rx_timer = {0};

/* ---------------------------------------------------------------------------
 * Kernel clock of a timer hanging off APB1.
 *
 * On STM32H7 the APB timer clock is PCLK1 when the APB1 prescaler is 1, and
 * 2 x PCLK1 otherwise - the prescaler is not visible from the PCLK value
 * alone, so the divider field is read back from RCC before deciding.
 * -------------------------------------------------------------------------*/
static uint32_t apb1_timer_kernel_hz(void)
{
    uint32_t pclk1_hz;
    uint32_t ppre1;

    pclk1_hz = HAL_RCC_GetPCLK1Freq();
    ppre1    = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) >> RCC_D2CFGR_D2PPRE1_Pos;

    /* The field is a divider *encoding*, not the divider itself: 0 is the only
     * "not divided" code (RCC_D2CFGR_D2PPRE1_DIV1), while DIV2 encodes as 4,
     * DIV4 as 5, DIV8 as 6 and DIV16 as 7. Any division at all doubles the
     * timer clock. */
    if (ppre1 == 0U)
    {
        return pclk1_hz;
    }

    return pclk1_hz * 2U;
}

/* ---------------------------------------------------------------------------
 * Pick PSC/ARR for the requested baud rate.
 *
 * PSC is left at 0 so the counter runs at the full kernel clock: that keeps
 * the quantisation error to a single tick, which at 200 MHz is 5 ns. For
 * 9600 baud one bit is ~20833 ticks, so the residual error stays far below
 * 0.01 %.
 * -------------------------------------------------------------------------*/
static int timer_calc_prescaler(uint32_t baudrate, sw_uart_timer_info_t *info)
{
    uint32_t kernel_hz;
    uint64_t ticks;

    if ((baudrate == 0U) || (baudrate > 4000000UL))
    {
        return -1;
    }

    kernel_hz = apb1_timer_kernel_hz();
    if (kernel_hz == 0U)
    {
        return -1;
    }

    /* Ticks per bit, rounded to nearest, then split into PSC/ARR. */
    ticks = ((uint64_t)kernel_hz + (baudrate / 2UL)) / baudrate;
    if ((ticks == 0U) || (ticks > 0xFFFFFFFFULL))
    {
        return -1;
    }

    info->baudrate     = baudrate;
    info->timer_clk_hz = kernel_hz;
    info->psc          = 0U;
    info->arr          = (uint32_t)(ticks - 1ULL);
    info->bit_ticks    = (uint32_t)ticks;
    /* Bit period actually produced by ticks, in ns:
     * ticks / kernel_hz seconds. */
    info->bit_ns       = (uint32_t)((ticks * 1000000000ULL + (kernel_hz / 2U))
                                    / kernel_hz);
    /* ticks/kernel_hz is the real bit period; baudrate/kernel_hz is the
     * ideal one. Their ratio minus 1, in percent. */
    info->error_pct    = ((double)ticks * (double)baudrate
                          / (double)kernel_hz - 1.0) * 100.0;
    return 0;
}

/* ------------------------------------------------------------------------ */
static int timer_base_init(TIM_TypeDef *tim, TIM_HandleTypeDef *h_out,
                           uint32_t psc, uint32_t arr)
{
    TIM_HandleTypeDef       h   = {0};
    TIM_MasterConfigTypeDef msk = {0};

    h.Instance = tim;
    h.Init.Prescaler         = psc;
    h.Init.CounterMode       = TIM_COUNTERMODE_UP;
    h.Init.Period            = arr;
    h.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    h.Init.RepetitionCounter = 0U;
    h.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&h) != HAL_OK)
    {
        return -1;
    }

    /* TRGO stays reset: the DMA request is driven by the update event, not
     * by a master trigger, so no master-slave wiring is needed. */
    msk.MasterOutputTrigger = TIM_TRGO_RESET;
    msk.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&h, &msk) != HAL_OK)
    {
        return -1;
    }

    /* Publish the handle so the start/stop helpers can use the __HAL_TIM_*
     * macros, which dereference ->Instance. */
    *h_out = h;

    /* Counter is left stopped: the caller decides when bit 0 may go out. */
    return 0;
}

/* ------------------------------------------------------------------------ */
int sw_uart_timer_init(uint32_t baudrate)
{
    if (timer_calc_prescaler(baudrate, &g_timer_info) != 0)
    {
        return -1;
    }

    SW_UART_TX_TIMER_CLK_ENABLE();
    SW_UART_RX_TIMER_CLK_ENABLE();

    if (timer_base_init(SW_UART_TX_TIMER, &g_tx_timer,
                        g_timer_info.psc, g_timer_info.arr) != 0)
    {
        return -1;
    }
    if (timer_base_init(SW_UART_RX_TIMER, &g_rx_timer,
                        g_timer_info.psc, g_timer_info.arr) != 0)
    {
        return -1;
    }

    /* Bit timing is the most latency-critical work here: give both timers the
     * top preempt priority so nothing can delay a bit boundary. */
    HAL_NVIC_SetPriority(SW_UART_TX_TIMER_IRQn,
                         SW_UART_IRQ_PRIORITY, SW_UART_IRQ_SUBPRIORITY);
    HAL_NVIC_SetPriority(SW_UART_RX_TIMER_IRQn,
                         SW_UART_IRQ_PRIORITY, SW_UART_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(SW_UART_TX_TIMER_IRQn);
    HAL_NVIC_EnableIRQ(SW_UART_RX_TIMER_IRQn);

    return 0;
}

/* ------------------------------------------------------------------------ */
void sw_uart_timer_deinit(void)
{
    sw_uart_timer_stop();
    HAL_NVIC_DisableIRQ(SW_UART_TX_TIMER_IRQn);
    HAL_NVIC_DisableIRQ(SW_UART_RX_TIMER_IRQn);
    g_timer_info = (sw_uart_timer_info_t){0};
}

/* ------------------------------------------------------------------------ */
void sw_uart_timer_start(void)
{
    __HAL_TIM_SET_COUNTER(&g_tx_timer, 0U);
    __HAL_TIM_SET_COUNTER(&g_rx_timer, 0U);
    __HAL_TIM_CLEAR_FLAG(&g_tx_timer, TIM_FLAG_UPDATE);
    __HAL_TIM_CLEAR_FLAG(&g_rx_timer, TIM_FLAG_UPDATE);
    /* UDE gates the update DMA request on H7: without it the timer still
     * rolls over and sets UIF, but no request reaches the stream and NDTR
     * never decrements - the stream stays armed forever. */
    __HAL_TIM_ENABLE_IT(&g_tx_timer, TIM_DIER_UDE);
    __HAL_TIM_ENABLE(&g_tx_timer);
}

void sw_uart_timer_stop(void)
{
    __HAL_TIM_DISABLE(&g_tx_timer);
    __HAL_TIM_DISABLE_IT(&g_tx_timer, TIM_DIER_UDE);
}

/* ------------------------------------------------------------------------ */
uint32_t sw_uart_timer_get_tick_hz(void)
{
    return g_timer_info.timer_clk_hz;
}

const sw_uart_timer_info_t *sw_uart_timer_get_info(void)
{
    return &g_timer_info;
}

/* ---------------------------------------------------------------------------
 * RX sampling clock.
 *
 * The period register is reloaded between slots, which is why the ARR
 * prescaler is always 0: the bit time has to be expressible as a plain tick
 * count so 1.5 bits and 1 bit share the same counter clock.
 * -------------------------------------------------------------------------*/
void sw_uart_timer_rx_start_half_bit(void)
{
    uint32_t ticks = (g_timer_info.bit_ticks * 3U) / 2U;

    __HAL_TIM_SET_COUNTER(&g_rx_timer, 0U);
    __HAL_TIM_SET_AUTORELOAD(&g_rx_timer, ticks - 1U);
    __HAL_TIM_SET_COUNTER(&g_rx_timer, 0U);
    __HAL_TIM_CLEAR_FLAG(&g_rx_timer, TIM_FLAG_UPDATE);
    __HAL_TIM_ENABLE(&g_rx_timer);
}

void sw_uart_timer_rx_reload_one_bit(void)
{
    /* ARR is preloaded, so the new period takes effect at the next update;
     * clearing UIF first keeps the pending event from firing twice. */
    __HAL_TIM_SET_AUTORELOAD(&g_rx_timer, g_timer_info.bit_ticks - 1U);
    __HAL_TIM_CLEAR_FLAG(&g_rx_timer, TIM_FLAG_UPDATE);
}

void sw_uart_timer_rx_stop(void)
{
    __HAL_TIM_DISABLE(&g_rx_timer);
}

TIM_HandleTypeDef *sw_uart_timer_rx_handle(void)
{
    return &g_rx_timer;
}
