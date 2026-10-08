/* ---------------------------------------------------------------------------
 * Software UART - TX FIFO, bit encoder and DMA chain.
 * -------------------------------------------------------------------------*/

#include "sw_uart_tx.h"
#include "sw_uart_gpio.h"
#include "sw_uart_timer.h"
#include "sw_uart_dma.h"
#include "stm32h7xx_hal.h"

/* The FIFO index arithmetic needs a power of two and a permanently free slot
 * so full and empty are distinguishable by index alone. */
#define SW_UART_TX_FIFO_MASK   (SW_UART_TX_BUFFER_SIZE - 1U)

/* Bytes encoded per DMA launch.
 *
 * One byte is ten words, so a chunk of 64 is 640 words - comfortably inside
 * the 16-bit NDTR field. Larger chunks mean fewer interrupts and therefore
 * less jitter between frames; the timer free-runs across a whole chunk, so
 * the CPU only has to stay ahead of the encoder, not of every bit. */
#define SW_UART_TX_CHUNK_BYTES 64U
#define SW_UART_TX_CHUNK_WORDS (SW_UART_TX_CHUNK_BYTES * SW_UART_BITS_PER_BYTE)

/* Staging area for the BSRR words of the bytes currently on the wire.
 *
 * volatile keeps the section populated under -Os: nothing reads these words
 * on the CPU side, so the compiler would drop the object and the build-time
 * DMA placement check would end up validating nothing. 32-byte aligned with a
 * size that is a multiple of 32, because the cache is maintained by address
 * over whole lines. */
static volatile uint32_t g_tx_words[SW_UART_TX_CHUNK_WORDS]
    __attribute__((section(".dma_buf"), aligned(32)));

/* Everything shared between the application and the DMA interrupt. */
typedef struct
{
    uint8_t  fifo[SW_UART_TX_BUFFER_SIZE];
    volatile uint16_t head;    /* advanced by the writer only           */
    volatile uint16_t tail;    /* advanced by the encoder, either ctx   */
    volatile sw_uart_tx_state_t state;
    uint32_t tx_bytes;
    uint32_t tx_dropped;
    uint32_t tx_frames;
    uint32_t dma_restart_count;
} tx_info_t;

static tx_info_t g_tx = {0};

/* Byte count of the chunk currently being shifted out. */
static volatile uint16_t g_tx_chunk_bytes;

/* True while the bit clock is free-running. The clock must not be restarted
 * per chunk: resetting CNT would delay the words still queued in the stream
 * and stretch the byte period. */
static bool g_tx_timer_running;

/* ---------------------------------------------------------------------------
 * Encode count bytes as count * 10 BSRR words.
 *
 * A BSRR write is unconditional: the bit position selects set or reset, so
 * there is no read-modify-write and no chance of writing a stale level back
 * between two bits of the same frame.
 * -------------------------------------------------------------------------*/
static void tx_encode_chunk(uint16_t count)
{
    uint16_t tail = g_tx.tail;
    uint32_t *w   = (uint32_t *)g_tx_words;

    for (uint16_t b = 0U; b < count; b++)
    {
        uint8_t  data = g_tx.fifo[(tail + b) & SW_UART_TX_FIFO_MASK];
        uint32_t *f   = &w[(uint32_t)b * SW_UART_BITS_PER_BYTE];

        f[0] = SW_UART_TX_BSRR_RESET;                  /* start bit, always 0 */
        for (uint8_t i = 0U; i < 8U; i++)
        {
            /* LSB first: bit 0 leaves the pin first. */
            f[1U + i] = ((data >> i) & 1U) ? SW_UART_TX_BSRR_SET
                                            : SW_UART_TX_BSRR_RESET;
        }
        f[9] = SW_UART_TX_BSRR_SET;                    /* stop bit, always 1 */
    }

    /* Publish the new tail only after the words are written, so the interrupt
     * cannot free FIFO space that the encoder is still reading. */
    g_tx.tail = (uint16_t)((tail + count) & SW_UART_TX_FIFO_MASK);
    g_tx.tx_frames += count;
}

/* ---------------------------------------------------------------------------
 * Launch the next chunk if there is anything to send and no transfer is
 * running. The single place where the DMA is started.
 * -------------------------------------------------------------------------*/
static void tx_pump(void)
{
    uint16_t pending;
    uint16_t count;

    if (g_tx.state == SW_UART_TX_ACTIVE)
    {
        return;                     /* the interrupt will chain the rest */
    }

    pending = (uint16_t)((g_tx.head - g_tx.tail) & SW_UART_TX_FIFO_MASK);
    if (pending == 0U)
    {
        return;
    }

    count = (pending < SW_UART_TX_CHUNK_BYTES) ? pending
                                               : SW_UART_TX_CHUNK_BYTES;
    tx_encode_chunk(count);

    /* Make the encoded words visible to the DMA before it can start. */
    SCB_CleanDCache_by_Addr((uint32_t *)g_tx_words,
                            (size_t)count * SW_UART_BITS_PER_BYTE
                                * sizeof(uint32_t));

    g_tx_chunk_bytes       = count;
    g_tx.state             = SW_UART_TX_ACTIVE;
    g_tx.dma_restart_count++;

    /* Park the line at idle before the stream starts, so the first word of
     * the chunk - the start bit - produces a real 1 -> 0 edge. Without this
     * the pin may still be low from the previous chunk's last data bit and
     * the receiver never sees a falling edge. */
    sw_uart_gpio_tx_set();

    if (sw_uart_dma_start((uint16_t)(count * SW_UART_BITS_PER_BYTE)) != 0)
    {
        /* Cannot happen with a configured stream; recover rather than wedge
         * the FIFO in ACTIVE forever. */
        g_tx_chunk_bytes = 0U;
        g_tx.state       = SW_UART_TX_IDLE;
        return;
    }

    /* The bit clock free-runs: it is started once when the stream goes from
     * idle to active and keeps counting across chunk boundaries. Restarting
     * it per chunk would reset CNT mid-frame, so the words already queued
     * would be shifted out late and the byte rate would drop below the
     * configured baud. */
    if (!g_tx_timer_running)
    {
        sw_uart_timer_start();
        g_tx_timer_running = true;
    }
}

/* ------------------------------------------------------------------------ */
int sw_uart_tx_init(void)
{
    g_tx = (tx_info_t){0};
    g_tx_chunk_bytes  = 0U;
    g_tx_timer_running = false;
    sw_uart_gpio_tx_set();

    return sw_uart_dma_set_buffer((const uint32_t *)g_tx_words);
}

/* ------------------------------------------------------------------------ */
void sw_uart_tx_deinit(void)
{
    sw_uart_tx_stop();
    sw_uart_dma_stop();
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_tx_space(void)
{
    uint16_t head = g_tx.head;
    uint16_t tail = g_tx.tail;

    return (size_t)(SW_UART_TX_BUFFER_SIZE - 1U
                    - (size_t)((head - tail) & SW_UART_TX_FIFO_MASK));
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_tx_pending(void)
{
    return (size_t)((g_tx.head - g_tx.tail) & SW_UART_TX_FIFO_MASK);
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_tx_write(const uint8_t *data, size_t len)
{
    uint16_t head;
    size_t   space;
    size_t   n;

    if ((data == NULL) || (len == 0U))
    {
        return 0U;
    }

    head  = g_tx.head;
    space = sw_uart_tx_space();

    n = (len < space) ? len : space;

    for (size_t i = 0U; i < n; i++)
    {
        g_tx.fifo[(head + i) & SW_UART_TX_FIFO_MASK] = data[i];
    }

    /* Publish the data before the head moves, so the consumer never sees a
     * count that points at bytes that are not written yet. */
    g_tx.head = (uint16_t)((head + n) & SW_UART_TX_FIFO_MASK);
    g_tx.tx_bytes  += (uint32_t)n;
    g_tx.tx_dropped += (uint32_t)(len - n);

    tx_pump();
    return n;
}

/* ------------------------------------------------------------------------ */
void sw_uart_tx_stop(void)
{
    sw_uart_timer_stop();
    sw_uart_dma_stop();
    sw_uart_gpio_tx_set();     /* the line must not end mid-frame */
    g_tx_chunk_bytes  = 0U;
    g_tx.state        = SW_UART_TX_IDLE;
    g_tx_timer_running = false;
}

/* ------------------------------------------------------------------------ */
static sw_uart_tx_stats_t g_tx_stats;

const sw_uart_tx_stats_t *sw_uart_tx_get_stats(void)
{
    g_tx_stats.tx_bytes          = g_tx.tx_bytes;
    g_tx_stats.tx_dropped        = g_tx.tx_dropped;
    g_tx_stats.dma_restart_count = g_tx.dma_restart_count;
    g_tx_stats.tx_frames         = g_tx.tx_frames;
    return &g_tx_stats;
}
sw_uart_tx_state_t sw_uart_tx_get_state(void)
{
    return g_tx.state;
}

/* ---------------------------------------------------------------------------
 * Transfer complete: chain the next chunk, or park the line when the FIFO is
 * empty. Runs in the DMA interrupt.
 * -------------------------------------------------------------------------*/
void sw_uart_tx_on_dma_done(void)
{
    if (g_tx.state != SW_UART_TX_ACTIVE)
    {
        return;
    }

    g_tx_chunk_bytes = 0U;
    g_tx.state       = SW_UART_TX_IDLE;

    if (sw_uart_tx_pending() > 0U)
    {
        tx_pump();
        return;
    }

    sw_uart_tx_stop();
}
