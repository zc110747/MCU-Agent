/* ---------------------------------------------------------------------------
 * Software UART - TX bit-period self-test.
 *
 * No logic analyser is available and the ST-Link VCP proved unreliable during
 * bring-up, so the TX waveform is measured where it actually exists: on the
 * pin. PA10 is sampled in a tight polling loop and every edge is timestamped
 * with the DWT cycle counter.
 *
 * The firmware streams 0x55, which is 1 0101 0101, so every bit boundary
 * produces an edge and the interval between consecutive edges *is* the bit
 * period. Averaging over many edges gives the bit clock as seen on the real
 * pad, independent of what the code intended.
 *
 * Why a polling loop and not a timer interrupt
 * --------------------------------------------
 * Sampling from SysTick (1 kHz) against a 104.17 us bit period is a ratio of
 * exactly 9.6, so the sample points land on the same ten phases forever. The
 * pin then appears to sit at one level even though it is toggling: the
 * readings alias. An earlier version of this test reported a bit period of
 * zero for exactly that reason. Polling samples every few tens of
 * nanoseconds, far finer than the bit period, so it cannot alias. The DMA
 * interrupt still preempts the loop, which only skips a few cycles near each
 * interrupt and does not disturb the phase relationship between edges.
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "sw_uart_gpio.h"
#include "sw_uart_timer.h"
#include "stm32h7xx_hal.h"

/* Target edge count; each 0x55 bit yields one edge. */
#define SELFTEST_EDGES      400U

/* Wall-clock budget so the measurement always terminates. */
#define SELFTEST_TIMEOUT_MS 5000U

/* Published for the SWD read-back. */
typedef struct
{
    uint32_t magic;
    uint32_t done;
    uint32_t timed_out;
    uint32_t edges_measured;
    uint32_t bit_period_min;      /* CPU cycles between edges */
    uint32_t bit_period_max;
    uint32_t bit_period_sum;
    uint32_t nominal;             /* expected cycles per bit  */
    uint32_t out_of_range;
    uint32_t edges_seen;        /* every edge, including the first */
    uint32_t level_or;          /* OR of sampled levels: proves both states */
} selftest_result_t;

#define SELFTEST_MAGIC 0x5746524DUL   /* "WFRM" */

volatile selftest_result_t g_selftest = {0};

static uint8_t  s_last_level;
static uint32_t s_last_edge;
static uint32_t s_start_ms;

static void dwt_start(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Cycles per bit the timer is expected to produce. */
static uint32_t nominal_cycles(void)
{
    /* f_cpu * bit_period. The bit period is known in nanoseconds, so
     * cycles = f_cpu * bit_ns / 1e9. The product reaches ~4.2e13, well past
     * uint32, so the multiply has to be done in 64 bits - in 32 bits it wraps
     * and yields zero, which makes every real edge look out of range. */
    return (uint32_t)(((uint64_t)400000000ULL
                       * sw_uart_timer_get_info()->bit_ns) / 1000000000ULL);
}

/* ---------------------------------------------------------------------------
 * Start a capture.
 * -------------------------------------------------------------------------*/
void sw_uart_tx_selftest_start(void)
{
    g_selftest = (selftest_result_t){0};
    g_selftest.magic          = SELFTEST_MAGIC;
    g_selftest.nominal        = nominal_cycles();
    g_selftest.bit_period_min = 0xFFFFFFFFUL;

    s_last_level = sw_uart_gpio_tx_read();
    s_last_edge  = DWT->CYCCNT;
    s_start_ms   = HAL_GetTick();

    dwt_start();
}

/* ---------------------------------------------------------------------------
 * Sample for one bounded slice, then return.
 *
 * The slice is short enough that the application still gets to run often, and
 * the caller is expected to keep the transmitter fed between calls - a 512 B
 * burst only lasts about half a second at 9600 baud, which is shorter than the
 * time needed to gather several hundred edges.
 *
 * Returns non-zero once the capture is complete.
 * -------------------------------------------------------------------------*/
uint32_t sw_uart_tx_selftest_step(void)
{
    /* Bounded slice: at 400 MHz this is roughly a quarter of a millisecond of
     * polling, long enough to catch several edges at 9600 baud. */
    uint32_t guard = 160000U;

    while (guard-- > 0U)
    {
        uint8_t now_level = sw_uart_gpio_tx_read();

        g_selftest.level_or |= now_level;

        if (now_level != s_last_level)
        {
            uint32_t now   = DWT->CYCCNT;
            uint32_t width = now - s_last_edge;

            s_last_level = now_level;
            s_last_edge  = now;
            g_selftest.edges_seen++;

            /* The first edge is skipped when timing: the interval before it
             * depends on whatever the pin was doing beforehand. */
            if (g_selftest.edges_seen > 1U)
            {
                if (width < g_selftest.bit_period_min)
                {
                    g_selftest.bit_period_min = width;
                }
                if (width > g_selftest.bit_period_max)
                {
                    g_selftest.bit_period_max = width;
                }
                g_selftest.bit_period_sum  += width;
                g_selftest.edges_measured++;

                /* +/-2% around nominal. Anything wider means a bit was
                 * stretched, dropped or duplicated. */
                if ((width < (g_selftest.nominal - (g_selftest.nominal / 50U)))
                    || (width > (g_selftest.nominal + (g_selftest.nominal / 50U))))
                {
                    g_selftest.out_of_range++;
                }
            }
        }
    }

    if (g_selftest.edges_measured >= SELFTEST_EDGES)
    {
        g_selftest.done = 1U;
    }
    else if ((HAL_GetTick() - s_start_ms) > SELFTEST_TIMEOUT_MS)
    {
        g_selftest.timed_out = 1U;
        g_selftest.done      = 1U;
    }

    return g_selftest.done;
}

uint32_t sw_uart_tx_selftest_avg(void)
{
    return (g_selftest.edges_measured > 0U)
           ? (g_selftest.bit_period_sum / g_selftest.edges_measured)
           : 0U;
}
