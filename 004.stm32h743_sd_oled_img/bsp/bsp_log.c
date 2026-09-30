/**
 ******************************************************************************
 * @file    bsp_log.c
 * @brief   USART1 console (PA9/PA10, 115200-8-N-1) + non-blocking PRINT_LOG.
 *
 *   printf_log() is a drop-in replacement for printf() that formats into a
 *   stack-local buffer, then pushes the bytes into a TX ring buffer. A
 *   transmit (TXE) interrupt drains the ring buffer byte-by-byte, so the
 *   caller never blocks on the UART.
 *
 *   Design notes:
 *   - The formatting buffer (LOG_BUF_SIZE) covers all current log lines;
 *     longer lines are truncated by vsnprintf (it returns the would-be len).
 *   - uart_write() updates the shared ring-buffer indices inside a critical
 *     section where the UART TX interrupt is DISABLED, so the ISR cannot
 *     race on those indices. This is the "close the serial interrupt during
 *     the write" requirement.
 *   - Only UART_IT_TXE is ever enabled; RX and error interrupts stay off.
 ******************************************************************************
 */

#include "bsp_log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define LOG_UART_TIMEOUT_MS 100U

/* Per-call format buffer. 256 bytes covers all current log lines. */
#define LOG_BUF_SIZE 256

/* ---------------------------------------------------------------------------
 * TX ring buffer (interrupt-driven, non-blocking)
 * ------------------------------------------------------------------------- */
#define UART_TX_BUF_SIZE 1024U

/* TX 环形缓冲状态：写入方填充、TX 中断消费 */
typedef struct
{
    uint8_t           buf[UART_TX_BUF_SIZE];
    volatile uint16_t w;      /* next write slot (uart_write) */
    volatile uint16_t r;      /* next read  slot (ISR)        */
    volatile uint16_t n;      /* bytes pending in the ring    */
    volatile uint8_t  active; /* 1 = a transmission is running */
    uint8_t           nvic_on; /* USART1 NVIC enabled?         */
} log_tx_ring_t;

static log_tx_ring_t g_log_tx = {0};

/* Enable the USART1 global interrupt once (idempotent). */
void log_uart_init(void)
{
    if (!g_log_tx.nvic_on)
    {
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        g_log_tx.nvic_on = 1U;
    }
}

/* Push bytes into the ring buffer. Called from thread mode only.
 * The UART TX interrupt is disabled while we touch the shared indices. */
static int uart_write(const uint8_t *data, int len)
{
    if ((data == NULL) || (len <= 0))
    {
        return 0;
    }

    log_uart_init();

    /* CRITICAL SECTION: close the serial TX interrupt so the ISR cannot
     * modify g_log_tx.r / g_log_tx.n while we are appending. */
    __HAL_UART_DISABLE_IT(&huart1, UART_IT_TXE);

    int written = 0;
    while ((written < len) && (g_log_tx.n < UART_TX_BUF_SIZE))
    {
        g_log_tx.buf[g_log_tx.w] = data[written++];
        g_log_tx.w              = (g_log_tx.w + 1U) % UART_TX_BUF_SIZE;
        g_log_tx.n++;
    }

    /* If the transmitter is idle, prime the first byte. g_log_tx.active == 0
     * guarantees TDR is empty, so writing it is safe. The ISR then drains
     * the rest. */
    if (!g_log_tx.active && (g_log_tx.n > 0U))
    {
        g_log_tx.active       = 1U;
        huart1.Instance->TDR = g_log_tx.buf[g_log_tx.r];
        g_log_tx.r            = (g_log_tx.r + 1U) % UART_TX_BUF_SIZE;
        g_log_tx.n--;
    }

    /* Re-open the serial TX interrupt; it fires once TDR is empty. */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_TXE);

    return written;
}

/* Drain one byte per TXE event. Called from USART1_IRQHandler(). */
void log_uart_tx_irq(void)
{
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_TXE) &&
        __HAL_UART_GET_IT_SOURCE(&huart1, UART_IT_TXE))
    {
        if (g_log_tx.n > 0U)
        {
            huart1.Instance->TDR = g_log_tx.buf[g_log_tx.r];
            g_log_tx.r            = (g_log_tx.r + 1U) % UART_TX_BUF_SIZE;
            g_log_tx.n--;
        }
        else
        {
            /* Nothing left to send: stop the TX interrupt. */
            __HAL_UART_DISABLE_IT(&huart1, UART_IT_TXE);
            g_log_tx.active = 0U;
        }
    }
}

/* ---- newlib retarget (kept so any stray printf() also stays non-blocking) -- */

int _write(int file, char *ptr, int len)
{
    (void)file;
    uart_write((const uint8_t *)ptr, len);
    return len;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

GlobalType_t bsp_log_init(void)
{
    huart1.Instance                    = USART1;
    huart1.Init.BaudRate               = 115200;
    huart1.Init.WordLength             = UART_WORDLENGTH_8B;
    huart1.Init.StopBits               = UART_STOPBITS_1;
    huart1.Init.Parity                 = UART_PARITY_NONE;
    huart1.Init.Mode                   = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling           = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        return RT_FAIL;
    }
    if (HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        return RT_FAIL;
    }
    if (HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
    {
        return RT_FAIL;
    }
    if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
    {
        return RT_FAIL;
    }

    /* printf() must not wait for a full line buffer (used only for stray newlib). */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Arm the USART1 TX interrupt that drains the ring buffer. */
    log_uart_init();

    return RT_OK;
}

void bsp_log_write(const char *data, int len)
{
    if (data == NULL || len <= 0)
    {
        return;
    }
    /* Route through the non-blocking ring buffer instead of blocking on the UART. */
    uart_write((const uint8_t *)data, len);
}

void vprintf_log(const char *fmt, va_list ap)
{
#if PRINT_LOG_ENABLE == 0
    (void)fmt;
    (void)ap;
    return;
#else
    char buf[LOG_BUF_SIZE];
    int  n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0)
        return;
    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    uart_write((const uint8_t *)buf, n);
#endif
}

void printf_log(const char *fmt, ...)
{
#if PRINT_LOG_ENABLE == 0
    (void)fmt;
    return;
#else
    va_list ap;
    va_start(ap, fmt);
    vprintf_log(fmt, ap);
    va_end(ap);
#endif
}
