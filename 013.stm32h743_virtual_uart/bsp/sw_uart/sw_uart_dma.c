/* ---------------------------------------------------------------------------
 * Software UART - TX bit-stream DMA abstraction.
 * -------------------------------------------------------------------------*/

#include "sw_uart_dma.h"
#include "sw_uart_gpio.h"
#include "sw_uart_tx.h"
#include "stm32h7xx_hal.h"

static DMA_HandleTypeDef g_tx_dma = {0};

/* BSRR words owned by the stream. HAL_DMA_Start_IT takes both addresses as
 * arguments, so the memory side is re-armed on every launch. */
static const uint32_t *g_tx_words;

/* Invoked from the transfer-complete interrupt. */
static void (*g_tx_done_cb)(void);

/* ------------------------------------------------------------------------ */
int sw_uart_dma_set_buffer(const uint32_t *bsrr_words)
{
    if (bsrr_words == NULL)
    {
        return -1;
    }

    g_tx_words = bsrr_words;
    return 0;
}

/* ------------------------------------------------------------------------ */
int sw_uart_dma_init(void)
{
    if (g_tx_words == NULL)
    {
        return -1;
    }

    SW_UART_TX_DMA_CLK_ENABLE();

    g_tx_dma.Instance                 = SW_UART_TX_DMA_STREAM;
    g_tx_dma.Init.Request             = SW_UART_TX_DMA_REQUEST;
    g_tx_dma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    g_tx_dma.Init.PeriphInc           = DMA_PINC_DISABLE;  /* BSRR is fixed */
    g_tx_dma.Init.MemInc              = DMA_MINC_ENABLE;   /* next BSRR word */
    g_tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    g_tx_dma.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
    g_tx_dma.Init.Mode                = DMA_NORMAL;
    g_tx_dma.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
    /* Direct mode, not FIFO. In FIFO mode the controller waits for the
     * threshold before touching the peripheral, delaying the start bit
     * relative to the timer edge that requested it - exactly the jitter a
     * bit-banged UART cannot tolerate. */
    g_tx_dma.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    g_tx_dma.Init.MemBurst            = DMA_MBURST_SINGLE;
    g_tx_dma.Init.PeriphBurst         = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&g_tx_dma) != HAL_OK)
    {
        return -1;
    }

    HAL_NVIC_SetPriority(SW_UART_TX_DMA_IRQn,
                         SW_UART_IRQ_PRIORITY, SW_UART_IRQ_SUBPRIORITY);
    HAL_NVIC_EnableIRQ(SW_UART_TX_DMA_IRQn);

    g_tx_done_cb = sw_uart_tx_on_dma_done;
    return 0;
}

/* ------------------------------------------------------------------------ */
int sw_uart_dma_start(uint16_t item_count)
{
    if ((g_tx_words == NULL) || (item_count == 0U))
    {
        return -1;
    }

    __HAL_DMA_DISABLE(&g_tx_dma);
    /* H7 HAL takes both addresses explicitly: for MEMORY_TO_PERIPH the source
     * is the memory word and the destination is the fixed BSRR register. */
    if (HAL_DMA_Start_IT(&g_tx_dma,
                         (uint32_t)g_tx_words,
                         (uint32_t)&SW_UART_TX_PORT->BSRR,
                         item_count) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------------ */
void sw_uart_dma_stop(void)
{
    __HAL_DMA_DISABLE(&g_tx_dma);
}

bool sw_uart_dma_is_busy(void)
{
    return g_tx_dma.State == HAL_DMA_STATE_READY ? false : true;
}

/* ------------------------------------------------------------------------ */
void DMA1_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&g_tx_dma);

    if (g_tx_done_cb != NULL)
    {
        g_tx_done_cb();
    }
}
