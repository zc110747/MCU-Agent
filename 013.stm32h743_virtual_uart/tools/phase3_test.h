/* ---------------------------------------------------------------------------
 * Software UART - Phase 3: TX FIFO semantics and DMA chain acceptance.
 *
 * The write contract is never-blocking: what does not fit is refused and
 * counted, and the accepted length is returned. T1 and T3 drive that contract
 * directly; T5 keeps the producer ahead of the consumer for two full rings so
 * the FIFO wraps and the DMA chain re-arms many times.
 *
 * Results land in g_phase3_probe and are read back over SWD. The byte order
 * of the wrapped stream is judged from the PC, where the bytes are observable.
 * -------------------------------------------------------------------------*/
#ifndef PHASE3_TEST_H
#define PHASE3_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Published for the SWD read-back. */
typedef struct
{
    uint32_t magic;
    uint32_t state;

    /* T1 - empty FIFO: every offered length is accepted whole until the ring
     * is full, then refused and counted. */
    uint32_t t1_w1;
    uint32_t t1_w16;
    uint32_t t1_w256;
    uint32_t t1_w4096;
    uint32_t t1_w8_again;
    uint32_t t1_space;
    uint32_t t1_pending;
    uint32_t t1_dropped;
    uint32_t t1_frames;      /* must be 1: no chunk completed mid-test */
    uint32_t t1_pass;

    /* T3 - a write one byte past the ring capacity. */
    uint32_t t3_w4097;
    uint32_t t3_dropped;
    uint32_t t3_pass;

    /* T5 - two full rings with the producer kept ahead of the consumer. */
    uint32_t t5_total;
    uint32_t t5_written;
    uint32_t t5_pass;

    /* Aggregate. */
    uint32_t fail_count;
    uint32_t done;

    /* Final counters. tx_bytes == tx_frames proves every accepted byte was
     * encoded and shifted out; tx_dropped is what the contract refused. */
    uint32_t tx_bytes;
    uint32_t tx_frames;
    uint32_t tx_dropped;
    uint32_t dma_restart_count;
} PHASE3_PROBE_T;

/* Run T1 and arm the state machine. Call right after sw_uart_init(), before
 * anything has had a chance to drain the FIFO. */
void phase3_test_start(void);

/* Advance one slice. Non-zero once every check has finished. */
uint32_t phase3_test_step(void);

/* Read-only view of the running results. volatile because the fields are
 * written by the test state machine and read back asynchronously over SWD. */
const volatile PHASE3_PROBE_T *phase3_test_probe(void);

#ifdef __cplusplus
}
#endif

#endif /* PHASE3_TEST_H */
