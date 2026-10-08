/* ---------------------------------------------------------------------------
 * Software UART - Phase 4/5 RX acceptance implementation. See rx_test.h.
 * -------------------------------------------------------------------------*/

#include "rx_test.h"
#include "sw_uart.h"
#include "sw_uart_rx.h"
#include "bsp.h"

/* A stalled host must not wedge the run: report what did arrive and finish. */
#define RX_TEST_TIMEOUT_MS   20000U

/* Bytes pulled out of the RX FIFO per slice. */
#define RX_TEST_SLICE        64U

/* FNV-1a parameters, chosen so the host can recompute the same value with a
 * four-line loop. A plain sum alone cannot see transpositions. */
#define RX_FNV_OFFSET        2166136261UL
#define RX_FNV_PRIME         16777619UL

volatile RX_TEST_PROBE_T g_rx_test = {0};

static uint32_t s_deadline;

/* ------------------------------------------------------------------------ */
void rx_test_start(uint32_t expected)
{
    g_rx_test          = (RX_TEST_PROBE_T){0};
    g_rx_test.magic    = RX_TEST_MAGIC;
    g_rx_test.expected = expected;
    g_rx_test.first    = 0xFFFFFFFFUL;
    g_rx_test.last     = 0xFFFFFFFFUL;
    g_rx_test.hash     = RX_FNV_OFFSET;
    s_deadline         = board_millis() + RX_TEST_TIMEOUT_MS;
}

/* ------------------------------------------------------------------------ */
static void note(uint8_t b)
{
    if (g_rx_test.first == 0xFFFFFFFFUL)
    {
        g_rx_test.first = b;
    }
    g_rx_test.last  = b;
    g_rx_test.sum  += b;
    g_rx_test.hash  = (g_rx_test.hash ^ b) * RX_FNV_PRIME;
}

/* ------------------------------------------------------------------------
 * Hand the record to the TX path.
 *
 * sw_uart_write() is never blocking: it takes a prefix and returns its length,
 * so the remainder is offered again. The record is far smaller than the FIFO,
 * so this normally completes on the first call.
 * ---------------------------------------------------------------------*/
static void publish_record(void)
{
    const uint8_t *p     = (const uint8_t *)&g_rx_test;
    size_t         total = sizeof(g_rx_test);
    size_t         off   = 0U;

    while (off < total)
    {
        off += sw_uart_write(p + off, total - off);
    }
}

/* ------------------------------------------------------------------------ */
uint32_t rx_test_step(void)
{
    uint8_t buf[RX_TEST_SLICE];
    size_t  n;

    if (g_rx_test.done != 0U)
    {
        return 1U;
    }

    n = sw_uart_read(buf, sizeof(buf));
    for (size_t i = 0U; i < n; i++)
    {
        note(buf[i]);
    }
    g_rx_test.received += (uint32_t)n;

    /* Re-read the driver counters rather than trusting the local tally: these
     * are what proves nothing was silently dropped downstream. */
    g_rx_test.overflow    = sw_uart_rx_get_stats()->rx_overflow;
    g_rx_test.frame_error = sw_uart_rx_get_stats()->rx_frame_error;

    if ((g_rx_test.received >= g_rx_test.expected)
        || ((int32_t)(board_millis() - s_deadline) >= 0))
    {
        g_rx_test.done = 1U;
        publish_record();
        return 1U;
    }

    return 0U;
}

/* ------------------------------------------------------------------------ */
const volatile RX_TEST_PROBE_T *rx_test_probe(void)
{
    return &g_rx_test;
}
