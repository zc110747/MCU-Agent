/* ---------------------------------------------------------------------------
 * STM32H743 software UART - Phase 0 bring-up.
 *
 * Brings up the clocks, the PA9/PA10 software-UART pins and the bit-timing
 * timer, then blinks the board LED. The resolved timing is published in
 * g_phase0_probe so it can be read back over SWD and checked against the
 * theoretical numbers, which is the only verification available without a
 * logic analyser on the bench.
 *
 * No UART data path exists yet: TX/RX are Phase 1 and later.
 * -------------------------------------------------------------------------*/

#include "bsp.h"
#include "sw_uart.h"
#include "sw_uart_timer.h"
#include "sw_uart_gpio.h"

/* Published for the Phase 0 SWD read-back check. */
typedef struct
{
    uint32_t magic;          /* set to PHASE0_PROBE_MAGIC once init is done */
    uint32_t sysclk_hz;
    uint32_t hclk_hz;
    uint32_t pclk1_hz;
    uint32_t timer_kernel_hz;
    uint32_t baudrate;
    uint32_t psc;
    uint32_t arr;
    uint32_t bit_ns;
    uint32_t heartbeat;      /* incremented every 500 ms while running      */
} phase0_probe_t;

#define PHASE0_PROBE_MAGIC 0x50485330UL   /* "PHS0" */

volatile phase0_probe_t g_phase0_probe = {0};

int main(void)
{
    bsp_init();

    if (sw_uart_init(&SW_UART_DEFAULT_CONFIG) != 0)
    {
        /* Init failed: park the LED on so the state is visible on the bench
         * and park the probe so SWD can read the faulting state. */
        board_led_write(true);
        while (1)
        {
        }
    }

    const sw_uart_timer_info_t *info = sw_uart_timer_get_info();
    g_phase0_probe.sysclk_hz       = g_sysclk_hz;
    g_phase0_probe.hclk_hz         = g_hclk_hz;
    g_phase0_probe.pclk1_hz        = g_pclk1_hz;
    g_phase0_probe.timer_kernel_hz = info->timer_clk_hz;
    g_phase0_probe.baudrate        = info->baudrate;
    g_phase0_probe.psc             = info->psc;
    g_phase0_probe.arr             = info->arr;
    g_phase0_probe.bit_ns          = info->bit_ns;
    g_phase0_probe.magic           = PHASE0_PROBE_MAGIC;

    while (1)
    {
        board_led_toggle();
        g_phase0_probe.heartbeat++;
        board_delay_ms(500U);
    }
}
