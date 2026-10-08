/* ---------------------------------------------------------------------------
 * STM32H743 software UART - Phase 3 entry point.
 *
 * Runs the TX FIFO / DMA-chain acceptance once at start-up, then keeps an
 * incrementing pattern streaming so the PC can check byte order across ring
 * wraps (tools/verify_phase3.py).
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "sw_uart.h"
#include "phase3_test.h"

#define STEADY_SEQ_LEN 256U

int main(void)
{
    uint8_t seq[STEADY_SEQ_LEN];

    bsp_init();

    if (sw_uart_init(&SW_UART_DEFAULT_CONFIG) != 0)
    {
        board_led_write(true);
        while (1)
        {
        }
    }

    /* T1 has to run before anything drains the FIFO, so it executes inside
     * start() rather than being scheduled by the loop below. */
    phase3_test_start();
    while (phase3_test_step() == 0U)
    {
        board_delay_ms(1U);
    }

    /* Steady state: the same wrap-sensitive stream the test checked, now
     * regenerated forever so the PC can keep watching.
     *
     * The FIFO applies back-pressure by accepting a prefix and returning its
     * length, so an offer has to resume where the previous one stopped.
     * Re-sending the whole 256-byte block from index 0 after a partial accept
     * restarts the pattern mid-block and puts a 63 -> 0 jump on the wire every
     * time the ring refills - exactly the discontinuity the PC-side check
     * reported. Advance the offset by the accepted count instead. */
    for (uint32_t i = 0U; i < STEADY_SEQ_LEN; i++)
    {
        seq[i] = (uint8_t)i;
    }

    uint32_t off = 0U;

    while (1)
    {
        size_t n = sw_uart_write(&seq[off], STEADY_SEQ_LEN - off);

        off += (uint32_t)n;

        if (off >= STEADY_SEQ_LEN)
        {
            off = 0U;
            board_led_toggle();
        }
        else if (n == 0U)
        {
            board_delay_ms(1U);   /* FIFO full: wait for the DMA to drain */
        }
    }
}
