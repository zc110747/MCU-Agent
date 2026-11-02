/**
 * @file    bsp_uart.c
 * @brief   USART1 (PA9/PA10) debug console with a scheduler-aware TX path.
 * @note    两条发送路径与惰性互斥量: document/stm32f429/video_player_port.md A7
 */
#include "bsp_uart.h"
#include "FreeRTOS.h" /* core FreeRTOS definitions */
#include "task.h"     /* taskENTER_CRITICAL / xTaskGetSchedulerState */
#include "semphr.h"
#include <string.h>

void Error_Handler(void); /* defined in main.c */

UART_HandleTypeDef huart1;

/* TX ring + drain flags + serialisation mutex. */
typedef struct
{
    uint8_t           buf[UART_TX_BUF_SIZE]; /* ring storage */
    volatile uint16_t head;                  /* written by uart_write */
    volatile uint16_t tail;                  /* read by the TXE ISR */
    volatile uint8_t  busy;                  /* transmitter active */
    SemaphoreHandle_t mutex;                 /* NULL until first post-sched use */
} UART_TX_STATE_T;

/* RX ring filled by the RXNE ISR, drained by ui_task. */
typedef struct
{
    uint8_t           buf[UART_RX_BUF_SIZE]; /* ring storage */
    volatile uint16_t head;                  /* written by the RXNE ISR */
    volatile uint16_t tail;                  /* read by uart_read */
} UART_RX_STATE_T;

/* Everything the console owns. */
typedef struct
{
    UART_TX_STATE_T  tx;
    UART_RX_STATE_T  rx;
    volatile uint8_t ready; /* USART1 configured */
} UART_STATE_T;

static UART_STATE_T s_uart = {0};

static inline uint16_t tx_used(void)
{
    return (uint16_t)((s_uart.tx.head - s_uart.tx.tail) & (UART_TX_BUF_SIZE - 1));
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
    if (s_uart.tx.mutex == NULL)
    {
        s_uart.tx.mutex = xSemaphoreCreateMutex();
    }
    taskEXIT_CRITICAL();

    return s_uart.tx.mutex;
}

void BSP_UART_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin       = GPIO_PIN_9 | GPIO_PIN_10; /* PA9=TX PA10=RX */
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
        Error_Handler();
    }

    /* TXE drains the ring, RXNE fills s_uart.rx.buf.  No FreeRTOS call in the
     * ISR, so the priority does not have to sit below BASEPRI. */
    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    /* RXNEIE drains received bytes into the ring from now on. */
    SET_BIT(huart1.Instance->CR1, USART_CR1_RXNEIE);

    s_uart.ready = 1;
}

int uart_write(const uint8_t *data, int len)
{
    int               queued = 0;
    SemaphoreHandle_t mtx;

    /* Pre-scheduler: nobody drains the ring, so transmit polled instead.
     * See document/stm32f429/video_player_port.md A7. */
    if ((s_uart.ready == 0) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
    {
        if (s_uart.ready == 0)
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
    for (int i = 0; i < len; i++)
    {
        uint16_t next = (uint16_t)((s_uart.tx.head + 1) & (UART_TX_BUF_SIZE - 1));
        if (next == s_uart.tx.tail)
            break; /* full -> drop the remainder */
        s_uart.tx.buf[s_uart.tx.head] = data[i];
        s_uart.tx.head                = next;
        queued++;
    }
    if (s_uart.tx.busy == 0 && s_uart.tx.head != s_uart.tx.tail)
    {
        s_uart.tx.busy = 1;
        SET_BIT(huart1.Instance->CR1, USART_CR1_TXEIE);
    }
    taskEXIT_CRITICAL();

    if (mtx != NULL)
    {
        xSemaphoreGive(mtx);
    }

    return queued;
}

int uart_puts(const char *s)
{
    return uart_write((const uint8_t *)s, (int)strlen(s));
}

void uart_flush(void)
{
    uint32_t guard = 200000U; /* ~1 s of spins: never hang the caller */

    /* Wait for the ring to drain and the transmitter to go idle. */
    while ((s_uart.tx.busy != 0 || tx_used() != 0) && (guard-- > 0U))
    {
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            vTaskDelay(pdMS_TO_TICKS(1)); /* let the ISR make progress */
        }
    }
}

int uart_read(uint8_t *buf, int max)
{
    int n = 0;

    if ((buf == NULL) || (max <= 0))
    {
        return 0;
    }

    taskENTER_CRITICAL();
    while ((n < max) && (s_uart.rx.tail != s_uart.rx.head))
    {
        buf[n]         = s_uart.rx.buf[s_uart.rx.tail];
        s_uart.rx.tail = (uint16_t)((s_uart.rx.tail + 1) & (UART_RX_BUF_SIZE - 1));
        n++;
    }
    taskEXIT_CRITICAL();

    return n;
}

/* Real USART1 vector: overrides the weak Default_Handler in the startup .s. */
void USART1_IRQHandler(void)
{
    BSP_UART_IRQHandler();
}

void BSP_UART_IRQHandler(void)
{
    uint32_t sr = READ_REG(huart1.Instance->SR);

    if ((sr & USART_SR_TXE) != 0)
    {
        if (s_uart.tx.tail != s_uart.tx.head)
        {
            huart1.Instance->DR = s_uart.tx.buf[s_uart.tx.tail];
            s_uart.tx.tail      = (uint16_t)((s_uart.tx.tail + 1) & (UART_TX_BUF_SIZE - 1));
        }
        else
        {
            CLEAR_BIT(huart1.Instance->CR1, USART_CR1_TXEIE);
            s_uart.tx.busy = 0;
        }
    }

    /* RX: push into the command ring.  Reading DR also clears ORE, which would
     * otherwise keep the vector asserted forever after an overrun. */
    if ((sr & USART_SR_RXNE) != 0)
    {
        uint8_t  byte = (uint8_t)(huart1.Instance->DR & 0xFFU);
        uint16_t next = (uint16_t)((s_uart.rx.head + 1) & (UART_RX_BUF_SIZE - 1));

        if (next != s_uart.rx.tail) /* full -> drop, caller keeps its old data */
        {
            s_uart.rx.buf[s_uart.rx.head] = byte;
            s_uart.rx.head                = next;
        }
    }
}
