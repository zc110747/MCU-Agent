/* Minimal STM32F429 demo firmware for CMSIS-DAP probe verification.
 *
 * It does no peripheral access on purpose: a board-agnostic program that
 *  - writes a known signature to g_marker,
 *  - runs a short deterministic loop (sum 0..99 -> 4950),
 *  - then spins incrementing g_counter forever.
 * This lets us prove, through the probe, that a flashed program actually
 * *runs* (g_marker set, g_counter growing) and gives a stable spot to set a
 * breakpoint (the g_counter++ line) and to single-step. */

volatile unsigned int g_marker  = 0;
volatile unsigned int g_counter = 0;

/* DBGMCU base (RM0090, §37). Enabling the debug in low-power bits keeps the
 * debug domain clocked across Sleep/Stop/Standby, so the probe can always halt
 * and reflash the part - without this a firmware that idles in a low-power mode
 * would let OpenOCD's download step fail with
 * "the corresponding core might be turned off". */
#define DBGMCU_BASE        0xE0042000U
#define DBGMCU_CR          (*(volatile unsigned int *)(DBGMCU_BASE + 0x04U))

#define DBGMCU_CR_DBG_SLEEP   (1U << 0)
#define DBGMCU_CR_DBG_STOP    (1U << 1)
#define DBGMCU_CR_DBG_STANDBY (1U << 2)

static void dbg_keep_clocked(void) {
    DBGMCU_CR |= (DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY);
}

int main(void) {
    dbg_keep_clocked();             /* never let low-power modes gate the debug clock */

    g_marker = 0x12345678;          /* boot signature */

    unsigned int sum = 0;
    for (unsigned int i = 0; i < 100; i++) {
        sum += i;                   /* 0 + ... + 99 = 4950 */
    }
    g_counter = sum;                /* deterministic milestone */

    while (1) {
        g_counter++;                /* breakpoint / single-step target */
    }
}
