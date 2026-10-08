/* ---------------------------------------------------------------------------
 * STM32H743 software UART - Phase 4/5 entry point.
 *
 * Receives a fixed-length burst from the host and hands back a checksum
 * record, so the RX path can be judged from the same port that carried the
 * stimulus - no logic analyser, and no dependency on SWD reading cache-coherent
 * memory while the capture is still running.
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "sw_uart.h"
#include "rx_test.h"

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

    rx_test_start(RX_TEST_EXPECTED);
    while (rx_test_step() == 0U)
    {
    }

    /* Capture done and the record is queued; heartbeat so the end state is
     * visible on the board. */
    while (1)
    {
        board_led_toggle();
        board_delay_ms(250U);
    }
}
