/* ---------------------------------------------------------------------------
 * Software UART - Phase 4/5 RX acceptance.
 *
 * The RX path is judged against bytes the host actually sent, not against a
 * pattern regenerated on both sides: the PC knows the octets it put on the
 * wire, so it can recompute the sum and hash of the capture and compare.
 *
 * Results are published two ways:
 *   - g_rx_test, readable over SWD while the capture runs;
 *   - a fixed 40-byte little-endian record transmitted over TX once the
 *     capture finishes, so the verdict can be read from the same serial port
 *     that carried the stimulus. (SWD alone is not enough: the D-cache is on,
 *     so dirty probe lines may not be in physical memory yet.)
 * -------------------------------------------------------------------------*/
#ifndef RX_TEST_H
#define RX_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How many bytes a single run expects to receive. */
#define RX_TEST_EXPECTED   4096U

/* Sent as the first word of the record so the host can find its start. */
#define RX_TEST_MAGIC      0x52583454UL   /* "RX4T" */

typedef struct
{
    uint32_t magic;        /* RX_TEST_MAGIC                               */
    uint32_t expected;     /* bytes the run was told to wait for          */
    uint32_t received;     /* bytes actually drained out of the RX FIFO   */
    uint32_t overflow;     /* RX FIFO was full, frames dropped            */
    uint32_t frame_error;  /* start/stop bit violated                     */
    uint32_t sum;          /* sum of received bytes, mod 2^32             */
    uint32_t hash;         /* FNV-1a 32 over the received bytes           */
    uint32_t first;        /* first byte, 0xFFFFFFFF when none            */
    uint32_t last;         /* last byte, 0xFFFFFFFF when none             */
    uint32_t done;         /* capture finished                            */
} RX_TEST_PROBE_T;

/* Begin a capture of `expected` bytes. */
void rx_test_start(uint32_t expected);

/* Drain the RX FIFO once. Non-zero once the capture is finished and the
 * record has been handed to the TX path. */
uint32_t rx_test_step(void);

/* Read-only view, for the SWD probe. */
const volatile RX_TEST_PROBE_T *rx_test_probe(void);

#ifdef __cplusplus
}
#endif

#endif /* RX_TEST_H */
