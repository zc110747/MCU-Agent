/* ---------------------------------------------------------------------------
 * Software UART - top level.
 *
 * TX path: application -> 4096 B FIFO -> bit encoder -> DMA -> GPIO BSRR.
 * RX path: EXTI -> timer sampling -> 1024 B FIFO -> application.
 *
 * The FIFO never blocks: a write that does not fit returns the accepted
 * length and counts the drop, so the caller decides what to do about it.
 * -------------------------------------------------------------------------*/

#include "sw_uart.h"
#include "sw_uart_gpio.h"
#include "sw_uart_timer.h"
#include "sw_uart_dma.h"
#include "sw_uart_tx.h"
#include "sw_uart_rx.h"

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
 * The post-build check fails unless a .dma_buf section exists in non-DTCM RAM,
 * and it must survive -Os: as a plain array the store below is dead (nothing
 * reads it back), so the object and the whole section would be dropped and the
 * Release build would silently stop checking DMA placement. */
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

    g_stats = (sw_uart_stats_t){0};
    g_dma_probe[0] = SW_UART_TX_BSRR_SET;

    sw_uart_gpio_init();

    if (sw_uart_timer_init(config->baudrate) != 0)
    {
        return -1;
    }

    if (sw_uart_tx_init() != 0)
    {
        return -1;
    }
    if (sw_uart_dma_init() != 0)
    {
        return -1;
    }

    if (sw_uart_rx_init() != 0)
    {
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------------ */
void sw_uart_deinit(void)
{
    sw_uart_rx_deinit();
    sw_uart_tx_deinit();
    sw_uart_timer_deinit();
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_write(const uint8_t *data, size_t len)
{
    return sw_uart_tx_write(data, len);
}

/* ------------------------------------------------------------------------ */
size_t sw_uart_read(uint8_t *data, size_t len)
{
    return sw_uart_rx_read(data, len);
}

size_t sw_uart_available(void)
{
    return sw_uart_rx_available();
}

size_t sw_uart_tx_free(void)
{
    return sw_uart_tx_space();
}

/* ------------------------------------------------------------------------ */
const sw_uart_stats_t *sw_uart_get_stats(void)
{
    /* Fold the submodule counters into the snapshot the caller sees, so the
     * stats struct is the single place to look for every error count. */
    const sw_uart_tx_stats_t *tx = sw_uart_tx_get_stats();
    const sw_uart_rx_stats_t *rx = sw_uart_rx_get_stats();

    g_stats.tx_bytes        = tx->tx_bytes;
    g_stats.tx_overflow     = tx->tx_dropped;
    g_stats.dma_restart_count = tx->dma_restart_count;
    g_stats.rx_bytes        = rx->rx_bytes;
    g_stats.rx_overflow     = rx->rx_overflow;
    g_stats.rx_frame_error  = rx->rx_frame_error;

    return &g_stats;
}
