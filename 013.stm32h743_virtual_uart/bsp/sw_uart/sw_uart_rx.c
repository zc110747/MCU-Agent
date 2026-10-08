/* ---------------------------------------------------------------------------
 * Software UART - RX start-bit detection, sampling and frame parsing.
 *
 * Why EXTI only triggers on the start bit
 * --------------------------------------
 * A naive "sample on every edge" receiver breaks as soon as two adjacent data
 * bits carry the same value, because the line then produces no edge at all.
 * So EXTI is used for exactly one job: spotting the 1 -> 0 transition that
 * can only be a start bit. From that instant the timer owns the timing and
 * samples the line at 1.5, 2.5 ... 9.5 bit times, which is immune to the data
 * pattern entirely.
 *
 * Sampling instants, measured from the falling edge at t=0:
 *   t = 0.0 bit  start bit begins
 *   t = 1.5 bit  centre of the start bit  -> must read 0
 *   t = 2.5 bit  D0
 *   ...
 *   t = 9.5 bit  D7
 *   t = 10.5 bit centre of the stop bit   -> must read 1
 *
 * The first delay is one and a half bit times so the start bit is sampled in
 * its middle rather than at the edge it just crossed.
 * -------------------------------------------------------------------------*/

#include "sw_uart_rx.h"
#include "sw_uart_gpio.h"
#include "sw_uart_timer.h"
#include "stm32h7xx_hal.h"

#define SW_UART_RX_FIFO_MASK   (SW_UART_RX_BUFFER_SIZE - 1U)

/* Bits collected so far and the byte being assembled. */
typedef struct
{
    uint8_t  fifo[SW_UART_RX_BUFFER_SIZE];
    volatile uint16_t head;    /* advanced by the sampling interrupt */
    volatile uint16_t tail;    /* advanced by the application         */
    volatile sw_uart_rx_state_t state;
    uint8_t  shift;            /* partially assembled byte            */
    uint8_t  bit_count;        /* data bits collected so far (0..8)   */
    uint32_t rx_bytes;
    uint32_t rx_overflow;
    uint32_t rx_frame_error;
} rx_info_t;

static rx_info_t g_rx = {0};

static sw_uart_rx_stats_t g_rx_stats;

/* ------------------------------------------------------------------------ */
static void rx_push(uint8_t data)
{
    uint16_t head = g_rx.head;
    uint16_t next = (uint16_t)((head + 1U) & SW_UART_RX_FIFO_MASK);

    if (next == g_rx.tail)
    {
        /* FIFO full: drop the new byte rather than overwrite old data, and
         * record it so a stress run can prove it happened. */
        g_rx.rx_overflow++;
        return;
    }

    g_rx.fifo[head] = data;
    g_rx.head      = next;
    g_rx.rx_bytes++;
}

/* ------------------------------------------------------------------------ */
int sw_uart_rx_init(void)
{
    g_rx = (rx_info_t){0};

    HAL_NVIC_SetPriority(EXTI15_10_IRQn,
                         SW_UART_IRQ_PRIORITY, SW_UART_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    /* The update interrupt is what actually samples the line; EXTI only
     * arms it, so this one must stay enabled for RX to work at all. */
    __HAL_TIM_ENABLE_IT(sw_uart_timer_rx_handle(), TIM_IT_UPDATE);

    return 0;
}

/* ------------------------------------------------------------------------ */
void sw_uart_rx_deinit(void)
{
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    g_rx.state = SW_UART_RX_IDLE;
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_rx_available(void)
{
    return (size_t)((g_rx.head - g_rx.tail) & SW_UART_RX_FIFO_MASK);
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_rx_free(void)
{
    return (size_t)(SW_UART_RX_BUFFER_SIZE - 1U - sw_uart_rx_available());
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_rx_read(uint8_t *data, size_t len)
{
    uint16_t tail = g_rx.tail;
    size_t   n;

    if ((data == NULL) || (len == 0U))
    {
        return 0U;
    }

    n = sw_uart_rx_available();
    if (n > len)
    {
        n = len;
    }

    for (size_t i = 0U; i < n; i++)
    {
        data[i] = g_rx.fifo[(tail + i) & SW_UART_RX_FIFO_MASK];
    }

    /* Only move the tail after the bytes are copied out. */
    g_rx.tail = (uint16_t)((tail + n) & SW_UART_RX_FIFO_MASK);
    return n;
}

/* ------------------------------------------------------------------------ */
const sw_uart_rx_stats_t *sw_uart_rx_get_stats(void)
{
    g_rx_stats.rx_bytes       = g_rx.rx_bytes;
    g_rx_stats.rx_overflow    = g_rx.rx_overflow;
    g_rx_stats.rx_frame_error = g_rx.rx_frame_error;
    return &g_rx_stats;
}

sw_uart_rx_state_t sw_uart_rx_get_state(void)
{
    return g_rx.state;
}

/* ---------------------------------------------------------------------------
 * EXTI15_10 - PA10 falling edge.
 *
 * A falling edge is a candidate start bit. The line is re-checked here
 * because a glitch or a previous frame's stop edge can also produce one; the
 * sampling timer is only started once the idle level was genuinely high.
 * -------------------------------------------------------------------------*/
void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(SW_UART_RX_PIN);

    /* Ignore edges that arrive while a frame is already in progress: they are
     * data bits, not a new start bit. */
    if (g_rx.state != SW_UART_RX_IDLE)
    {
        return;
    }

    if (sw_uart_gpio_rx_read() != 0U)
    {
        return;     /* line is high, so this was not a start bit */
    }

    g_rx.state     = SW_UART_RX_START;
    g_rx.shift     = 0U;
    g_rx.bit_count = 0U;

    sw_uart_timer_rx_start_half_bit();
}

/* ---------------------------------------------------------------------------
 * TIM3 update - sample the line.
 *
 * Each interrupt consumes one bit slot. The first slot lands in the middle of
 * the start bit; after that the timer is reloaded for a single bit time so
 * every following sample lands in the middle of its own bit.
 * -------------------------------------------------------------------------*/
void TIM3_IRQHandler(void)
{
    uint8_t level;

    HAL_TIM_IRQHandler(sw_uart_timer_rx_handle());

    level = sw_uart_gpio_rx_read();

    switch (g_rx.state)
    {
    case SW_UART_RX_START:
        if (level != 0U)
        {
            /* Not a real start bit: false trigger, so no frame error. */
            g_rx.state = SW_UART_RX_IDLE;
            return;
        }
        g_rx.state     = SW_UART_RX_DATA;
        g_rx.bit_count = 0U;
        g_rx.shift     = 0U;
        sw_uart_timer_rx_reload_one_bit();
        return;

    case SW_UART_RX_DATA:
        if (level != 0U)
        {
            g_rx.shift |= (uint8_t)(1U << g_rx.bit_count);   /* LSB first */
        }
        g_rx.bit_count++;
        if (g_rx.bit_count >= 8U)
        {
            g_rx.state = SW_UART_RX_STOP;
        }
        sw_uart_timer_rx_reload_one_bit();
        return;

    case SW_UART_RX_STOP:
        if (level == 0U)
        {
            /* Stop bit low: the frame is malformed, so it must not reach the
             * FIFO as if it were good data. */
            g_rx.rx_frame_error++;
        }
        else
        {
            rx_push(g_rx.shift);
        }
        g_rx.state = SW_UART_RX_IDLE;
        sw_uart_timer_rx_stop();
        return;

    default:
        g_rx.state = SW_UART_RX_IDLE;
        return;
    }
}
