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
} phase2_probe_t;

#define PHASE2_PROBE_MAGIC 0x50485332UL   /* "PHS2" */

volatile phase2_probe_t g_phase2_probe = {0};

static const uint8_t g_pattern[4] = {0x55U, 0xAAU, 0x00U, 0xFFU};

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

    for (uint32_t i = 0U; i < BURST_BYTES; i++)
    {
        g_burst[i] = g_pattern[i & 3U];
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
