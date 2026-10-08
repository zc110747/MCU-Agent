/* ---------------------------------------------------------------------------
 * STM32H743 software UART - Phase 6/7 entry point.
 *
 * Full-duplex echo: everything the host sends comes back, in order, at the
 * same baud. Nothing is buffered beyond the two FIFOs, so a byte that fails to
 * make the round trip is a byte the RX path lost.
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "sw_uart.h"
#include "echo_test.h"

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

    echo_test_start();

    while (1)
    {
        echo_test_step();
    }
}
