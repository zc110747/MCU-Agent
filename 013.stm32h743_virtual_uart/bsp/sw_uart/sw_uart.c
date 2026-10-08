/* ---------------------------------------------------------------------------
 * Software UART - top level.
 *
 * Phase 0 scope: bring up the GPIO, timer and DMA resources and expose the
 * resolved bit timing. The FIFO, the bit encoder and the RX state machine are
 * deliberately not implemented yet, so the write/read paths report zero
 * rather than pretending to move data.
 * -------------------------------------------------------------------------*/

#include "sw_uart.h"
#include "sw_uart_gpio.h"
#include "sw_uart_timer.h"
#include "sw_uart_dma.h"

const sw_uart_config_t SW_UART_DEFAULT_CONFIG = {
    .baudrate  = SW_UART_DEFAULT_BAUD,
    .data_bits = 8U,
    .stop_bits = 1U,
    .parity    = SW_UART_PARITY_NONE,
};

/* Counters exposed through sw_uart_get_stats(). */
static sw_uart_stats_t g_stats = {0};

/* DMA reachability probe.
 *
 * The post-build check fails unless a .dma_buf section exists in
 * non-DTCM RAM, so a minimal one is declared here. It proves the placement
 * rules now instead of in Phase 2, and it is the buffer the TX bit encoder
 * will grow into.
 *
 * volatile keeps the section alive: as a plain array the store in
 * sw_uart_init is dead - nothing reads it back - so -Os drops the object,
 * the section disappears, and the Release build silently stops checking DMA
 * placement. 32-byte aligned with a 32-byte multiple size because the cache
 * is maintained by address over whole lines. */
static volatile uint32_t g_dma_probe[8] __attribute__((section(".dma_buf"),
                                                        aligned(32)));

/* ------------------------------------------------------------------------ */
int sw_uart_init(const sw_uart_config_t *config)
{
    if (config == NULL)
    {
        config = &SW_UART_DEFAULT_CONFIG;
    }
    if ((config->data_bits != 8U) || (config->stop_bits != 1U)
        || (config->parity != SW_UART_PARITY_NONE))
    {
        return -1;   /* only 8N1 is implemented */
    }

    sw_uart_gpio_init();

    if (sw_uart_timer_init(config->baudrate) != 0)
    {
        return -1;
    }

    g_dma_probe[0] = SW_UART_TX_BSRR_SET;
    g_stats = (sw_uart_stats_t){0};

    return 0;
}

/* ------------------------------------------------------------------------ */
void sw_uart_deinit(void)
{
    sw_uart_dma_stop();
    sw_uart_timer_deinit();
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_write(const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return 0U;
    }

    /* Phase 0: no FIFO yet, so nothing can be accepted. */
    (void)len;
    return 0U;
}

size_t sw_uart_read(uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return 0U;
    }

    return 0U;
}

size_t sw_uart_available(void)
{
    return 0U;
}

size_t sw_uart_tx_free(void)
{
    return 0U;
}

/* ------------------------------------------------------------------------ */
const sw_uart_stats_t *sw_uart_get_stats(void)
{
    return &g_stats;
}
