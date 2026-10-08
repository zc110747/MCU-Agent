/* ---------------------------------------------------------------------------
 * Software UART - Phase 6/7 echo acceptance.
 *
 * The loopback itself never blocks: the TX FIFO is what limits the echo, so a
 * byte is only taken out of the RX FIFO once the TX side has room for it. That
 * keeps the two rates matched (both run at the configured baud) and means a
 * missing byte in the host's trace can only come from the RX path, never from
 * the shim dropping it.
 *
 * The probe is read over SWD, so it is cache-line aligned and explicitly
 * cleaned: the D-cache is on, and a dirty line would otherwise hide the
 * counters from the debugger.
 * -------------------------------------------------------------------------*/
#ifndef ECHO_TEST_H
#define ECHO_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECHO_TEST_MAGIC   0x45434854UL   /* "ECHT" */

typedef struct
{
    uint32_t magic;
    uint32_t echoed;          /* bytes handed back to the TX path           */
    uint32_t rx_bytes;        /* driver count of received frames            */
    uint32_t tx_bytes;        /* driver count of queued bytes               */
    uint32_t rx_overflow;     /* RX FIFO was full                           */
    uint32_t rx_frame_error;  /* start/stop bit violated                    */
    uint32_t tx_overflow;     /* TX refused bytes                           */
    uint32_t loops;           /* slices run                                 */
} ECHO_PROBE_T;

/* Zero the probe at start-up. */
void echo_test_start(void);

/* Run one echo slice: RX FIFO -> TX FIFO, bounded by the TX free space. */
void echo_test_step(void);

/* Read-only view, for the SWD probe. */
const volatile ECHO_PROBE_T *echo_test_probe(void);

#ifdef __cplusplus
}
#endif

#endif /* ECHO_TEST_H */
