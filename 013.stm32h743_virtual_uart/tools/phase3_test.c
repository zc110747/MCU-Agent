/* ---------------------------------------------------------------------------
 * Software UART - Phase 3 test implementation. See phase3_test.h.
 * -------------------------------------------------------------------------*/

#include "phase3_test.h"
#include "sw_uart.h"
#include "sw_uart_tx.h"
#include "bsp.h"

#define PHASE3_MAGIC 0x50483354UL   /* "PH3T" */

/* Usable ring capacity: one slot stays free so full and empty are
 * distinguishable from the indices alone. */
#define PH3_RING_MAX (SW_UART_TX_BUFFER_SIZE - 1U)

/* Bytes still queued in the ring when the 4096-byte offer lands.
 *
 * The encoder empties the ring into the DMA staging buffer as soon as bytes
 * are offered, so write(1) leaves nothing behind; only the 16- and 256-byte
 * writes are still waiting at that point. */
#define PH3_T1_QUEUED (16U + 256U)

/* What the 4096-byte offer can still take, and what it must refuse. */
#define PH3_T1_TAKEN (PH3_RING_MAX - PH3_T1_QUEUED)
#define PH3_T1_REFUSED ((SW_UART_TX_BUFFER_SIZE - PH3_T1_TAKEN) + 8U)

/* Two full rings, so the head wraps and the ring is exercised end to end. */
#define PH3_T5_TOTAL (2U * SW_UART_TX_BUFFER_SIZE)

/* A stalled DMA must not wedge the test: fail and move on instead. */
#define PH3_DRAIN_TIMEOUT_MS 30000U

volatile PHASE3_PROBE_T g_phase3_probe = {0};

/* The stream the PC checks: index & 0xFF, so the FIFO order shows up as a
 * +1 step in the received bytes whatever the ring does underneath. */
static uint8_t g_pattern[PH3_T5_TOTAL];

typedef enum
{
    PH3_STATE_IDLE = 0,
    PH3_STATE_DRAIN1,
    PH3_STATE_T3,
    PH3_STATE_DRAIN3,
    PH3_STATE_T5,
    PH3_STATE_DRAIN5,
    PH3_STATE_DONE,
} ph3_state_t;

/* All state the sequence machine needs, kept together so it is reset in one
 * place (see mcu-code-style §3). */
typedef struct
{
    ph3_state_t state;
    uint32_t    t_start_ms;       /* watchdog base for the current drain */
    uint32_t    t5_written;
    uint32_t    dropped_before_t3;
} ph3_info_t;

static ph3_info_t g_info = {0};

/* ------------------------------------------------------------------------ */
static void check(uint32_t ok)
{
    if (ok == 0U)
    {
        g_phase3_probe.fail_count++;
    }
}

/* ------------------------------------------------------------------------
 * True once the FIFO is empty and the last chunk has left the wire.
 *
 * pending() alone is not enough: the encoder advances the tail before the
 * DMA has shifted the chunk out, so the state must be idle too.
 * ---------------------------------------------------------------------*/
static uint32_t drained(void)
{
    if ((sw_uart_tx_pending() == 0U)
        && (sw_uart_tx_get_state() == SW_UART_TX_IDLE))
    {
        return 1U;
    }
    if ((board_millis() - g_info.t_start_ms) > PH3_DRAIN_TIMEOUT_MS)
    {
        check(0U);            /* record the stall, but do not block forever */
        return 1U;
    }
    return 0U;
}

/* ------------------------------------------------------------------------
 * T1 - capacity and return values on an empty FIFO.
 *
 * The encoder drains the ring into the DMA staging buffer as it launches a
 * chunk, so tail moves inside these calls and the arithmetic is fixed by them
 * alone. The one thing that could disturb it is a chunk completing and
 * refilling mid-test: the first chunk is a single byte, ~1 ms on the wire,
 * while this sequence is a few hundred microseconds - and t1_frames == 1
 * proves that margin held.
 * ---------------------------------------------------------------------*/
void phase3_test_start(void)
{
    uint32_t dropped0;

    g_phase3_probe = (PHASE3_PROBE_T){0};
    g_phase3_probe.magic    = PHASE3_MAGIC;
    g_phase3_probe.t5_total = PH3_T5_TOTAL;
    g_info = (ph3_info_t){0};

    for (uint32_t i = 0U; i < PH3_T5_TOTAL; i++)
    {
        g_pattern[i] = (uint8_t)i;
    }

    dropped0 = sw_uart_tx_get_stats()->tx_dropped;

    g_phase3_probe.t1_w1       = (uint32_t)sw_uart_write(g_pattern, 1U);
    g_phase3_probe.t1_w16      = (uint32_t)sw_uart_write(g_pattern, 16U);
    g_phase3_probe.t1_w256     = (uint32_t)sw_uart_write(g_pattern, 256U);
    g_phase3_probe.t1_w4096    = (uint32_t)sw_uart_write(g_pattern,
                                                         SW_UART_TX_BUFFER_SIZE);
    g_phase3_probe.t1_w8_again = (uint32_t)sw_uart_write(g_pattern, 8U);
    g_phase3_probe.t1_space    = (uint32_t)sw_uart_tx_space();
    g_phase3_probe.t1_pending  = (uint32_t)sw_uart_tx_pending();
    g_phase3_probe.t1_dropped  = sw_uart_tx_get_stats()->tx_dropped - dropped0;
    g_phase3_probe.t1_frames   = sw_uart_tx_get_stats()->tx_frames;

    check(g_phase3_probe.t1_w1       == 1U);
    check(g_phase3_probe.t1_w16      == 16U);
    check(g_phase3_probe.t1_w256     == 256U);
    /* 272 bytes are queued by now, so the ring can only take 4095 - 272. */
    check(g_phase3_probe.t1_w4096    == PH3_T1_TAKEN);
    check(g_phase3_probe.t1_w8_again == 0U);
    check(g_phase3_probe.t1_space    == 0U);
    check(g_phase3_probe.t1_pending  == PH3_RING_MAX);
    /* 273 refused from the 4096 offer, then all 8. */
    check(g_phase3_probe.t1_dropped  == PH3_T1_REFUSED);
    /* Only the very first write reached the encoder; a second would mean a
     * DMA chunk completed while the test was running. */
    check(g_phase3_probe.t1_frames   == 1U);

    g_phase3_probe.t1_pass = (g_phase3_probe.fail_count == 0U) ? 1U : 0U;

    g_info.state         = PH3_STATE_DRAIN1;
    g_info.t_start_ms    = board_millis();
    g_phase3_probe.state = (uint32_t)PH3_STATE_DRAIN1;
}

/* ------------------------------------------------------------------------ */
uint32_t phase3_test_step(void)
{
    switch (g_info.state)
    {
    case PH3_STATE_DRAIN1:
        if (drained() != 0U)
        {
            g_info.dropped_before_t3 = sw_uart_tx_get_stats()->tx_dropped;
            g_info.state             = PH3_STATE_T3;
        }
        break;

    case PH3_STATE_T3:
    {
        uint32_t before = g_phase3_probe.fail_count;

        g_phase3_probe.t3_w4097 = (uint32_t)sw_uart_write(
            g_pattern, SW_UART_TX_BUFFER_SIZE + 1U);
        g_phase3_probe.t3_dropped =
            sw_uart_tx_get_stats()->tx_dropped - g_info.dropped_before_t3;

        check(g_phase3_probe.t3_w4097 == PH3_RING_MAX);
        /* The buffer holds 4096; the ring offers 4095, so 2 are refused. */
        check(g_phase3_probe.t3_dropped == 2U);

        g_phase3_probe.t3_pass =
            (g_phase3_probe.fail_count == before) ? 1U : 0U;

        g_info.state      = PH3_STATE_DRAIN3;
        g_info.t_start_ms = board_millis();
        break;
    }

    case PH3_STATE_DRAIN3:
        if (drained() != 0U)
        {
            g_info.t5_written = 0U;
            g_info.state      = PH3_STATE_T5;
            g_info.t_start_ms = board_millis();
        }
        break;

    case PH3_STATE_T5:
    {
        /* Offer everything that is left; the ring takes what it can and the
         * next slice offers the rest. This is the back-pressure path the
         * application uses, run against a consumer that never stops. */
        size_t n = sw_uart_write(g_pattern + g_info.t5_written,
                                 PH3_T5_TOTAL - g_info.t5_written);

        g_info.t5_written        += (uint32_t)n;
        g_phase3_probe.t5_written = g_info.t5_written;

        if (g_info.t5_written >= PH3_T5_TOTAL)
        {
            g_info.state      = PH3_STATE_DRAIN5;
            g_info.t_start_ms = board_millis();
        }
        break;
    }

    case PH3_STATE_DRAIN5:
        if (drained() != 0U)
        {
            const sw_uart_tx_stats_t *tx = sw_uart_tx_get_stats();
            uint32_t before = g_phase3_probe.fail_count;

            g_phase3_probe.tx_bytes          = tx->tx_bytes;
            g_phase3_probe.tx_frames         = tx->tx_frames;
            g_phase3_probe.tx_dropped        = tx->tx_dropped;
            g_phase3_probe.dma_restart_count = tx->dma_restart_count;

            /* Conservation: every accepted byte reached the encoder, so the
             * whole stream left the wire - nothing was lost in the chain. */
            check(g_phase3_probe.tx_bytes == g_phase3_probe.tx_frames);
            check(g_info.t5_written == PH3_T5_TOTAL);

            g_phase3_probe.t5_pass =
                (g_phase3_probe.fail_count == before) ? 1U : 0U;

            g_info.state         = PH3_STATE_DONE;
            g_phase3_probe.state = (uint32_t)PH3_STATE_DONE;
            g_phase3_probe.done  = 1U;
        }
        break;

    default:
        break;
    }

    return g_phase3_probe.done;
}

/* ------------------------------------------------------------------------ */
const volatile PHASE3_PROBE_T *phase3_test_probe(void)
{
    return &g_phase3_probe;
}
