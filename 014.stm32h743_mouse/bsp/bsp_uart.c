/* ---------------------------------------------------------------------------
 * bsp_uart.c - UART4, DMA circular receive + register-level transmit.
 *
 * Neither HAL_UART_Init() nor HAL_UART_Receive_DMA() is used. Both bring state
 * machines that fight this design: the UART HAL wants to own the DMA callback
 * chain (it overwrites XferCpltCallback inside Start_IT), and its circular
 * receive bookkeeping assumes a HAL_UART_STATE it never leaves correctly after
 * an IDLE. Writing the six registers directly is shorter than the workarounds.
 * -------------------------------------------------------------------------*/

#include "bsp_uart.h"
#include "bsp.h"

/* ---------------------------------------------------------------------------
 * Wiring
 * -------------------------------------------------------------------------*/
#define UART_PORT           GPIOA
#define UART_TX_PIN         GPIO_PIN_0     /* PA0 - AF8 */
#define UART_RX_PIN         GPIO_PIN_1     /* PA1 - AF8 */
#define UART_GPIO_AF        GPIO_AF8_UART4

#define UART_DMA_RX_STREAM  DMA1_Stream0
#define UART_DMA_RX_IRQn    DMA1_Stream0_IRQn
#define UART_DMA_RX_REQUEST DMA_REQUEST_UART4_RX

#define UART_IRQ_PRIO       5u

/* ---------------------------------------------------------------------------
 * Ring buffer
 * -------------------------------------------------------------------------*/
typedef struct
{
    uint8_t         *mem;
    uint32_t         size;      /* usable capacity is size - 1 */
    volatile uint32_t head;     /* producer writes here        */
    volatile uint32_t tail;     /* consumer reads here         */
} ring_t;

static uint8_t s_rx_mem[BSP_UART_RX_RING_SIZE];
static uint8_t s_tx_mem[BSP_UART_TX_RING_SIZE];

static ring_t s_rx_ring = { s_rx_mem, BSP_UART_RX_RING_SIZE, 0u, 0u };
static ring_t s_tx_ring = { s_tx_mem, BSP_UART_TX_RING_SIZE, 0u, 0u };

/* DMA staging buffer. Must be 32-byte aligned and a multiple of 32 bytes so a
 * by-address cache invalidate never touches a neighbouring object's line.
 * See "DMA buffer placement" in bsp.h for the pragma pair. */
BSP_DMA_BUF_BEGIN(".dma_buf")
static uint8_t s_dma_rx[BSP_UART_DMA_RX_SIZE] BSP_DMA_BUF_ATTR(".dma_buf");
BSP_DMA_BUF_END()

static DMA_HandleTypeDef s_hdma_rx;

/* Next byte the software has not yet lifted out of s_dma_rx. Only touched from
 * the UART/DMA interrupt handlers and from init, never from the main loop. */
static uint32_t s_rx_pos;

static bsp_uart_stats_t s_stats;

/* ------------------------------------------------------------------------ */
static bool ring_push(ring_t *ring, uint8_t value)
{
    uint32_t next = (ring->head + 1u) % ring->size;

    if (next == ring->tail)
    {
        return false;
    }

    ring->mem[ring->head] = value;
    ring->head = next;
    return true;
}

static bool ring_pop(ring_t *ring, uint8_t *value)
{
    if (ring->head == ring->tail)
    {
        return false;
    }

    *value = ring->mem[ring->tail];
    ring->tail = (ring->tail + 1u) % ring->size;
    return true;
}

static uint32_t ring_count(const ring_t *ring)
{
    uint32_t head = ring->head;
    uint32_t tail = ring->tail;

    return (head >= tail) ? (head - tail) : (ring->size - tail + head);
}

/* ---------------------------------------------------------------------------
 * Receive
 * -------------------------------------------------------------------------*/

/* Move every byte between s_rx_pos and `pos` (exclusive) into the RX ring.
 * `pos` is a position inside s_dma_rx. */
static void uart_drain_to(uint32_t pos)
{
    pos %= BSP_UART_DMA_RX_SIZE;

    /* The DMA wrote into AXI SRAM behind the D-cache. Drop the (stale) cached
     * copy before reading, otherwise the CPU may see pre-DMA contents. */
    SCB_InvalidateDCache_by_Addr((uint32_t *) s_dma_rx, (int32_t) BSP_UART_DMA_RX_SIZE);

    while (s_rx_pos != pos)
    {
        if (ring_push(&s_rx_ring, s_dma_rx[s_rx_pos]) == false)
        {
            s_stats.rx_overflow++;
        }
        s_stats.rx_bytes++;
        s_rx_pos = (s_rx_pos + 1u) % BSP_UART_DMA_RX_SIZE;
    }
}

/* Position of the DMA write pointer, derived from the remaining transfer
 * count. Valid only while a transfer is in flight in circular mode. */
static void uart_drain_from_ndtr(void)
{
    uint32_t remaining = ((DMA_Stream_TypeDef *) s_hdma_rx.Instance)->NDTR;
    uint32_t pos;

    if (remaining > BSP_UART_DMA_RX_SIZE)
    {
        remaining = BSP_UART_DMA_RX_SIZE;
    }

    pos = (BSP_UART_DMA_RX_SIZE - remaining) % BSP_UART_DMA_RX_SIZE;
    uart_drain_to(pos);
}

/* ---------------------------------------------------------------------------
 * DMA completion
 *
 * In circular mode the hardware reloads NDTR to the full size the instant it
 * reaches zero, so by the time this callback runs NDTR already reads "full"
 * again and deriving the position from it would drop the last byte of the
 * block. The transfer-complete event means exactly "the whole staging area is
 * full", so the correct target position is 0 - unconditionally.
 * -------------------------------------------------------------------------*/
static void uart_dma_rx_cplt(DMA_HandleTypeDef *hdma)
{
    (void) hdma;

    s_stats.dma_events++;
    uart_drain_to(0u);
}

static void uart_dma_rx_error(DMA_HandleTypeDef *hdma)
{
    (void) hdma;

    s_stats.frame_errors++;
    uart_drain_from_ndtr();
}

void UART4_IRQHandler(void)
{
    uint32_t isr = UART4->ISR;

    if ((isr & USART_ISR_IDLE) != 0u)
    {
        /* Clearing IDLE by writing IDLECF also drops the "line went quiet"
         * condition, so the next burst raises a fresh interrupt. */
        UART4->ICR = USART_ICR_IDLECF;
        s_stats.idle_events++;
        uart_drain_from_ndtr();
    }

    if ((isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0u)
    {
        /* Overrun means the DMA did not drain RDR fast enough. Clear it, count
         * it, and keep going: a single glitch must not kill the command port. */
        UART4->ICR = USART_ICR_ORECF | USART_ICR_FECF |
                     USART_ICR_NECF  | USART_ICR_PECF;
        s_stats.frame_errors++;
    }
}

void DMA1_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&s_hdma_rx);
}

/* ---------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------*/
static void uart_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0u};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin       = UART_TX_PIN | UART_RX_PIN;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;      /* keeps RX defined while nothing drives it */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = UART_GPIO_AF;
    HAL_GPIO_Init(UART_PORT, &gpio);
}

static void uart_dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    s_hdma_rx.Instance                 = UART_DMA_RX_STREAM;
    s_hdma_rx.Init.Request             = UART_DMA_RX_REQUEST;
    s_hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    s_hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    s_hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;
    s_hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    /* CIRCULAR: the receiver never stops, so the sink must never stop either. */
    s_hdma_rx.Init.Mode                = DMA_CIRCULAR;
    s_hdma_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    s_hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&s_hdma_rx) != HAL_OK)
    {
        /* No DMA means no command channel; USB output still works, so this is
         * not fatal enough to hang the device in an error trap. */
        return;
    }

    (void) HAL_DMA_Start_IT(&s_hdma_rx, (uint32_t) &UART4->RDR,
                            (uint32_t) s_dma_rx, BSP_UART_DMA_RX_SIZE);

    /* HAL_DMA_Start_IT() clears the callback pointers, so they must be
     * installed afterwards - setting them before is a silent no-op. */
    s_hdma_rx.XferCpltCallback = uart_dma_rx_cplt;
    s_hdma_rx.XferErrorCallback = uart_dma_rx_error;

    HAL_NVIC_SetPriority(UART_DMA_RX_IRQn, UART_IRQ_PRIO, 0u);
    HAL_NVIC_EnableIRQ(UART_DMA_RX_IRQn);
}

static void uart_peripheral_init(void)
{
    uint32_t pclk = HAL_RCC_GetPCLK1Freq();
    uint32_t brr  = (pclk + (BSP_UART_BAUDRATE / 2u)) / BSP_UART_BAUDRATE;

    __HAL_RCC_UART4_CLK_ENABLE();

    /* Order matters: the peripheral must be disabled while BRR and the control
     * registers are rewritten. */
    UART4->CR1 = 0u;
    UART4->CR3 = 0u;

    UART4->BRR = brr;
    UART4->CR1 = USART_CR1_TE | USART_CR1_RE;
    UART4->CR3 = USART_CR3_DMAR;      /* DMA request on RX; TX stays polled */

    UART4->CR1 |= USART_CR1_IDLEIE;   /* wake up when the line goes quiet */
    UART4->CR1 |= USART_CR1_UE;

    HAL_NVIC_SetPriority(UART4_IRQn, UART_IRQ_PRIO, 0u);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
}

void bsp_uart_init(void)
{
    s_stats.rx_bytes     = 0u;
    s_stats.tx_bytes     = 0u;
    s_stats.rx_overflow  = 0u;
    s_stats.tx_overflow  = 0u;
    s_stats.idle_events  = 0u;
    s_stats.dma_events   = 0u;
    s_stats.frame_errors = 0u;

    s_rx_ring.head = 0u;
    s_rx_ring.tail = 0u;
    s_tx_ring.head = 0u;
    s_tx_ring.tail = 0u;
    s_rx_pos = 0u;

    uart_gpio_init();
    uart_dma_init();
    uart_peripheral_init();
}

/* ---------------------------------------------------------------------------
 * Transmit / receive API
 * -------------------------------------------------------------------------*/
void bsp_uart_poll(void)
{
    uint8_t value;

    /* TXE_TXFNF means "TDR can take another byte". The loop stops the moment
     * the FIFO is full - it never spins, so the main loop keeps running. */
    while ((UART4->ISR & USART_ISR_TXE_TXFNF) != 0u)
    {
        if (ring_pop(&s_tx_ring, &value) == false)
        {
            break;
        }
        UART4->TDR = (uint32_t) value;
        s_stats.tx_bytes++;
    }
}

uint32_t bsp_uart_write(const uint8_t *src, uint32_t len)
{
    uint32_t written = 0u;

    if ((src == 0) || (len == 0u))
    {
        return 0u;
    }

    while (written < len)
    {
        if (ring_push(&s_tx_ring, src[written]) == false)
        {
            s_stats.tx_overflow++;
            break;
        }
        written++;
    }

    return written;
}

uint32_t bsp_uart_write_str(const char *text)
{
    uint32_t len = 0u;

    if (text == 0)
    {
        return 0u;
    }

    while (text[len] != '\0')
    {
        len++;
    }

    return bsp_uart_write((const uint8_t *) text, len);
}

uint32_t bsp_uart_read(uint8_t *dst, uint32_t max_len)
{
    uint32_t count = 0u;

    if ((dst == 0) || (max_len == 0u))
    {
        return 0u;
    }

    while (count < max_len)
    {
        if (ring_pop(&s_rx_ring, &dst[count]) == false)
        {
            break;
        }
        count++;
    }

    return count;
}

uint32_t bsp_uart_available(void)
{
    return ring_count(&s_rx_ring);
}

void bsp_uart_get_stats(bsp_uart_stats_t *stats)
{
    if (stats == 0)
    {
        return;
    }

    /* Snapshot outside the interrupt: a torn 32-bit read is impossible on
     * Cortex-M7, but copying the struct keeps the caller from holding a
     * pointer into memory the ISR keeps writing. */
    *stats = s_stats;
}
