/**
 ******************************************************************************
 * @file    bsp_uart.c
 * @brief   USART1 (PA9/PA10) debug console with a scheduler-aware TX path.
 *
 *  Transmit paths
 *  --------------
 *    scheduler NOT running (boot)  -> blocking polled HAL_UART_Transmit()
 *    scheduler RUNNING             -> mutex + critical section ring enqueue,
 *                                     drained by the TXE interrupt
 *
 *  Why the polled path matters: before vTaskStartScheduler() the TXE ISR may
 *  not be live / may be masked, and nobody polls the ring, so an enqueue-only
 *  path would silently swallow the boot banner.  Blocking transmit is slow
 *  but it is only used during start-up and it never drops a byte.
 *
 *  Why the mutex is LAZY: bsp_uart_init() runs before the SDRAM is up and
 *  therefore before vPortDefineHeapRegions(); xSemaphoreCreateMutex() there
 *  would allocate from an undefined heap.  The mutex is created on first use
 *  instead, i.e. on the first uart_write() that happens with the scheduler
 *  running - by then ucHeap is valid.  The critical section makes that lazy
 *  init race-free.
 ******************************************************************************
 */
#include "bsp_uart.h"
#include "FreeRTOS.h" /* core FreeRTOS definitions */
#include "task.h"     /* taskENTER_CRITICAL / xTaskGetSchedulerState */
#include "semphr.h"
#include <string.h>

void error_handler(void); /* defined in main.c */

UART_HandleTypeDef huart1;

/* Everything the TX path owns: ring + drain flags + serialisation mutex. */
typedef struct
{
    uint8_t           buf[UART_TX_BUF_SIZE]; /* ring storage */
    volatile uint16_t head;                  /* written by uart_write */
    volatile uint16_t tail;                  /* written by the TXE ISR */
    volatile uint8_t  busy;                  /* transmitter active */
    volatile uint8_t  ready;                 /* USART1 configured */
    SemaphoreHandle_t mutex;                 /* NULL until first post-sched use */
} UART_TX_STATE_T;

static UART_TX_STATE_T s_uart_tx = {0};

static inline uint16_t tx_used(void)
{
    return (uint16_t)((s_uart_tx.head - s_uart_tx.tail) & (UART_TX_BUF_SIZE - 1U));
}

/**
 * @brief  Return the TX mutex, creating it on first use.
 * @retval the mutex, or NULL if it could not be created (caller then runs
 *         unprotected, which is still correct - the critical section below
 *         keeps the ring itself consistent).
 */
static SemaphoreHandle_t tx_mutex_get(void)
{
    taskENTER_CRITICAL();
    if (s_uart_tx.mutex == NULL)
    {
        s_uart_tx.mutex = xSemaphoreCreateMutex();
    }
    taskEXIT_CRITICAL();

    return s_uart_tx.mutex;
}

void bsp_uart_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_9 | GPIO_PIN_10; /* PA9 = TX, PA10 = RX */
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 115200;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        error_handler();
    }

    /* Enable the TXE interrupt so the ring drains in the ISR.  Priority 6 sits
     * below configMAX_SYSCALL_INTERRUPT_PRIORITY; the TX path calls no FreeRTOS
     * API from the ISR, so this is safe. */
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    s_uart_tx.ready = 1U;
}

int32_t uart_write(const uint8_t *data, int32_t len)
{
    int32_t           queued = 0;
    SemaphoreHandle_t mtx;

    /* Boot stage (or UART not up yet): the TXE ISR is not draining the ring, so
     * enqueue-only would silently swallow the boot banner.  Use a blocking
     * polled transmit instead - slow, but it never drops a byte and it does not
     * need the interrupt or the scheduler. */
    if ((s_uart_tx.ready == 0U) || (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
    {
        if (s_uart_tx.ready == 0U)
        {
            return 0; /* nothing to transmit on */
        }
        HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, HAL_MAX_DELAY);
        return len;
    }

    mtx = tx_mutex_get();
    if (mtx != NULL)
    {
        if (xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE)
        {
            return 0;
        }
    }

    /* Critical section protects the ring counters + busy flag from the ISR.
     * The whole line is enqueued in one go, so log lines never interleave. */
    taskENTER_CRITICAL();
    for (int32_t i = 0; i < len; i++)
    {
        uint16_t next = (uint16_t)((s_uart_tx.head + 1U) & (UART_TX_BUF_SIZE - 1U));
        if (next == s_uart_tx.tail)
        {
            break; /* full -> drop the remainder */
        }
        s_uart_tx.buf[s_uart_tx.head] = data[i];
        s_uart_tx.head                = next;
        queued++;
    }
    if ((s_uart_tx.busy == 0U) && (s_uart_tx.head != s_uart_tx.tail))
    {
        s_uart_tx.busy = 1U;
        SET_BIT(huart1.Instance->CR1, USART_CR1_TXEIE);
    }
    taskEXIT_CRITICAL();

    if (mtx != NULL)
    {
        xSemaphoreGive(mtx);
    }

    return queued;
}

int32_t uart_puts(const char *s)
{
    return uart_write((const uint8_t *)s, (int32_t)strlen(s));
}

void uart_flush(void)
{
    uint32_t guard = 200000U; /* ~1 s of spins: never hang the caller */

    /* Wait until no bytes are pending and the transmitter has gone idle.
     * Before the scheduler is up the polled path already sent everything, so
     * this returns immediately in that case. */
    while (((s_uart_tx.busy != 0U) || (tx_used() != 0U)) && (guard-- > 0U))
    {
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            vTaskDelay(pdMS_TO_TICKS(1)); /* let the ISR make progress */
        }
    }
}

/* Real USART1 vector: overrides the weak Default_Handler in the startup .s. */
void USART1_IRQHandler(void)
{
    bsp_uart_irq_handler();
}

void bsp_uart_irq_handler(void)
{
    uint32_t sr = READ_REG(huart1.Instance->SR);

    if ((sr & USART_SR_TXE) != 0U)
    {
        if (s_uart_tx.tail != s_uart_tx.head)
        {
            huart1.Instance->DR = s_uart_tx.buf[s_uart_tx.tail];
            s_uart_tx.tail      = (uint16_t)((s_uart_tx.tail + 1U) & (UART_TX_BUF_SIZE - 1U));
        }
        else
        {
            CLEAR_BIT(huart1.Instance->CR1, USART_CR1_TXEIE);
            s_uart_tx.busy = 0U;
        }
    }

    /* RX: clear RXNE to avoid a sticky interrupt; not consumed by the demo. */
    if ((sr & USART_SR_RXNE) != 0U)
    {
        (void)huart1.Instance->DR;
    }
}
