/* ---------------------------------------------------------------------------
 * Software UART - Phase 6/7 echo acceptance implementation. See echo_test.h.
 * -------------------------------------------------------------------------*/

#include "echo_test.h"
#include "sw_uart.h"
#include "stm32h7xx_hal.h"

/* Bytes moved per slice. Small enough that a burst does not monopolise the
 * loop, large enough that the FIFO shuffling is not per byte. */
#define ECHO_SLICE        64U

/* How often the probe is written back to memory for the debugger. */
#define ECHO_CLEAN_PERIOD 1024U

/* 32-byte aligned and exactly 32 bytes, so cleaning it touches whole lines and
 * cannot drag an unrelated object into memory. */
volatile ECHO_PROBE_T g_echo __attribute__((aligned(32)));

static uint32_t s_clean_countdown;

/* ------------------------------------------------------------------------ */
void echo_test_start(void)
{
    g_echo            = (ECHO_PROBE_T){0};
    g_echo.magic      = ECHO_TEST_MAGIC;
    s_clean_countdown = ECHO_CLEAN_PERIOD;
}

/* ------------------------------------------------------------------------ */
void echo_test_step(void)
{
    uint8_t buf[ECHO_SLICE];
    size_t  space;
    size_t  want;
    size_t  n;

    space = sw_uart_tx_free();
    if (space == 0U)
    {
        /* TX is saturated: leave the RX FIFO alone so nothing is discarded. */
        return;
    }

    want = (space < ECHO_SLICE) ? space : ECHO_SLICE;
    n    = sw_uart_read(buf, want);
    if (n == 0U)
    {
        return;
    }

    /* sw_uart_write() takes min(len, free space). The free space only grows
     * while we are in here - the DMA interrupt advances the tail and nothing
     * advances the head - so the whole slice always fits. */
    (void)sw_uart_write(buf, n);

    g_echo.echoed += (uint32_t)n;
    g_echo.loops++;

    if (--s_clean_countdown == 0U)
    {
        const sw_uart_stats_t *st = sw_uart_get_stats();

        g_echo.rx_bytes       = st->rx_bytes;
        g_echo.tx_bytes       = st->tx_bytes;
        g_echo.rx_overflow    = st->rx_overflow;
        g_echo.rx_frame_error = st->rx_frame_error;
        g_echo.tx_overflow    = st->tx_overflow;

        SCB_CleanDCache_by_Addr((uint32_t *)&g_echo, sizeof(g_echo));
        s_clean_countdown = ECHO_CLEAN_PERIOD;
    }
}

/* ------------------------------------------------------------------------ */
const volatile ECHO_PROBE_T *echo_test_probe(void)
{
    return &g_echo;
}
