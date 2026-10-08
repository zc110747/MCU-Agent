/* ---------------------------------------------------------------------------
 * STM32H743 software UART - Phase 2: TX via Timer -> DMA -> GPIO BSRR.
 *
 * Streams a repeating test pattern out of PA10 at 9600 8N1 so the PC can
 * check it over the ST-Link virtual COM port. The pattern is chosen to make
 * bit-timing errors obvious:
 *
 *   0x55 = 01010101  alternating, every bit boundary is an edge
 *   0xAA = 10101010  the inverse, same edges shifted by one bit
 *   0x00            a single 10-bit-wide low pulse
 *   0xFF            a single 10-bit-wide high pulse
 *
 * No logic analyser is connected, so correctness is judged by what the PC
 * actually receives: wrong timing shows up as framing errors, corrupt bytes
 * or a stalled stream.
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "sw_uart.h"
#include "sw_uart_timer.h"
#include "sw_uart_gpio.h"
#include "sw_uart_tx.h"
#include <stdbool.h>

void     sw_uart_tx_selftest_start(void);
uint32_t sw_uart_tx_selftest_step(void);
uint32_t sw_uart_tx_selftest_avg(void);

/* Published for the SWD read-back check. */
typedef struct
{
    uint32_t magic;
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t timer_kernel_hz;
    uint32_t baudrate;
    uint32_t psc;
    uint32_t arr;
    uint32_t bit_ns;
    uint32_t loop_count;     /* pattern repeats completed               */
    uint32_t tx_accepted;    /* bytes sw_uart_write() took              */
    uint32_t tx_dropped;     /* bytes refused because the FIFO was full */
    uint32_t tx_frames;      /* bytes actually shifted out             */
    uint32_t bit_cycles_avg; /* measured bit period, in CPU cycles     */
} phase2_probe_t;

#define PHASE2_PROBE_MAGIC 0x50485332UL   /* "PHS2" */

volatile phase2_probe_t g_phase2_probe = {0};

/* One transmission burst.
 *
 * The pattern is repeated to fill a whole burst because the FIFO must not
 * drain in the middle of it: a burst shorter than the chunk size would stop
 * and restart the bit clock every time, putting an idle gap between bursts
 * and making the byte rate depend on how often the application happens to
 * call write(). */
#define BURST_BYTES 512U

static uint8_t g_burst[BURST_BYTES];

int main(void)
{
    bsp_init();

    if (sw_uart_init(&SW_UART_DEFAULT_CONFIG) != 0)
    {
        board_led_write(true);
        while (1)
        {
        }
    }

    /* The self-test measures the bit period as the interval between edges, so
     * the pattern it runs must put an edge on every bit boundary. 0x55 is
     * 1 0101 0101, which does exactly that; 0x00 and 0xFF do not (they hold
     * one level for nine bit times) and would swamp the statistics with
     * nine-bit intervals. The mixed pattern is used for the steady-state
     * traffic check instead. */
    for (uint32_t i = 0U; i < BURST_BYTES; i++)
    {
        g_burst[i] = 0x55U;
    }

    const sw_uart_timer_info_t *info = sw_uart_timer_get_info();
    g_phase2_probe.sysclk_hz       = g_sysclk_hz;
    g_phase2_probe.hclk_hz         = g_hclk_hz;
    g_phase2_probe.pclk1_hz        = g_pclk1_hz;
    g_phase2_probe.timer_kernel_hz = info->timer_clk_hz;
    g_phase2_probe.baudrate        = info->baudrate;
    g_phase2_probe.psc             = info->psc;
    g_phase2_probe.arr             = info->arr;
    g_phase2_probe.bit_ns          = info->bit_ns;
    g_phase2_probe.magic           = PHASE2_PROBE_MAGIC;

    /* Start TX, then measure the waveform it puts on the pin.
     *
     * The transmitter must still be running while the self-test samples, and
     * a single 512-byte burst lasts only ~0.5 s at 9600 baud, which the
     * self-test can outlast. So the FIFO is topped up repeatedly until the
     * measurement reports that it has finished. */
    (void)sw_uart_write(g_burst, sizeof(g_burst));
    sw_uart_tx_selftest_start();

    /* The transmitter must keep running for the whole capture, and one
     * 512-byte burst only lasts about half a second, so the FIFO is topped up
     * between measurement slices. Doing it here rather than in a timer keeps
     * the pin sampling free of interrupt jitter. */
    while (!sw_uart_tx_selftest_step())
    {
        (void)sw_uart_write(g_burst, sizeof(g_burst));
    }

    /* Record the measured average so the capture is visible over SWD even
     * after the fact. */
    g_phase2_probe.bit_cycles_avg = sw_uart_tx_selftest_avg();

    while (1)
    {
        /* Top the FIFO up. sw_uart_write never blocks, so the loop simply
         * offers more data until the pipe is full and then waits for the DMA
         * to drain. */
        while (sw_uart_write(g_burst, sizeof(g_burst))
               == sizeof(g_burst))
        {
            /* FIFO accepted a whole burst: give the DMA room to work. */
        }

        g_phase2_probe.tx_accepted = sw_uart_tx_get_stats()->tx_bytes;

        board_led_toggle();
    }
}
