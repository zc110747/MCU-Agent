/**
  ******************************************************************************
  * @file    bsp_log.c
  * @brief   Crash-safe formatted logging for the bare-metal STM32H743 build.
  *
  *   printf_log() is a drop-in replacement for printf() that formats into a
  *   stack-local buffer, then pushes the bytes into a TX ring buffer. A
  *   transmit (TXE) interrupt drains the ring buffer byte-by-byte, so the
  *   caller never blocks on the UART.
  *
  *   Ported from project 003.stm32h743_lvgl_oled (PRINT_LOG design):
  *   - uart_write() updates the shared ring-buffer indices inside a critical
  *     section where the UART TX interrupt is DISABLED, so the ISR cannot
  *     race on those indices.
  *   - Only UART_IT_TXE is ever enabled; RX and error interrupts stay off.
  *   001-specific: USART1 is fully initialized here (no CubeMX main.h / g_uart_log.huart),
  *   so a file-scope g_uart_log.huart is declared and configured by bsp_log_init().
  ******************************************************************************
  */
#include "bsp_log.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Per-call format buffer. 256 bytes covers all current log lines; longer
 * lines are truncated by vsnprintf (it returns the would-be length). */
#define LOG_BUF_SIZE 256

/* ---------------------------------------------------------------------------
 * TX ring buffer (interrupt-driven, non-blocking)
 * ------------------------------------------------------------------------- */
#define UART_TX_BUF_SIZE 1024U

typedef struct
{
  UART_HandleTypeDef huart;
  uint8_t  tx_buf[UART_TX_BUF_SIZE];
  volatile uint16_t tx_w;       /* next write slot (uart_write) */
  volatile uint16_t tx_r;       /* next read  slot (ISR)        */
  volatile uint16_t tx_n;       /* bytes pending in the ring    */
  volatile uint8_t  tx_active;  /* 1 = a transmission is running */
  uint8_t  tx_nvic_on;          /* USART1 NVIC enabled?         */
} UART_LOG_STATE_T;

static UART_LOG_STATE_T g_uart_log = {0};
/* Enable the USART1 global interrupt once (idempotent). */
static void uart_tx_enable_irq(void)
{
    if (!g_uart_log.tx_nvic_on)
    {
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
        g_uart_log.tx_nvic_on = 1U;
    }
}

/* Push bytes into the ring buffer. Called from thread mode only.
 * The UART TX interrupt is disabled while we touch the shared indices. */
static int32_t uart_write(const uint8_t *data, int32_t len)
{
    if ((data == NULL) || (len <= 0))
    {
        return 0;
    }

    uart_tx_enable_irq();

    /* CRITICAL SECTION: close the serial TX interrupt so the ISR cannot
     * modify g_uart_log.tx_r / g_uart_log.tx_n while we are appending. */
    __HAL_UART_DISABLE_IT(&g_uart_log.huart, UART_IT_TXE);

    int32_t written = 0;
    while ((written < len) && (g_uart_log.tx_n < UART_TX_BUF_SIZE))
    {
        g_uart_log.tx_buf[g_uart_log.tx_w] = data[written++];
        g_uart_log.tx_w = (g_uart_log.tx_w + 1U) % UART_TX_BUF_SIZE;
        g_uart_log.tx_n++;
    }

    /* If the transmitter is idle, prime the first byte. g_uart_log.tx_active == 0
     * guarantees TDR is empty, so writing it is safe. The ISR then drains
     * the rest. */
    if (!g_uart_log.tx_active && (g_uart_log.tx_n > 0U))
    {
        g_uart_log.tx_active = 1U;
        g_uart_log.huart.Instance->TDR = g_uart_log.tx_buf[g_uart_log.tx_r];
        g_uart_log.tx_r = (g_uart_log.tx_r + 1U) % UART_TX_BUF_SIZE;
        g_uart_log.tx_n--;
    }

    /* Re-open the serial TX interrupt; it fires once TDR is empty. */
    __HAL_UART_ENABLE_IT(&g_uart_log.huart, UART_IT_TXE);

    return written;
}

/* Drain one byte per TXE event. Called from USART1_IRQHandler(). */
void log_uart_tx_irq(void)
{
    if (__HAL_UART_GET_FLAG(&g_uart_log.huart, UART_FLAG_TXE) &&
        __HAL_UART_GET_IT_SOURCE(&g_uart_log.huart, UART_IT_TXE))
    {
        if (g_uart_log.tx_n > 0U)
        {
            g_uart_log.huart.Instance->TDR = g_uart_log.tx_buf[g_uart_log.tx_r];
            g_uart_log.tx_r = (g_uart_log.tx_r + 1U) % UART_TX_BUF_SIZE;
            g_uart_log.tx_n--;
        }
        else
        {
            /* Nothing left to send: stop the TX interrupt. */
            __HAL_UART_DISABLE_IT(&g_uart_log.huart, UART_IT_TXE);
            g_uart_log.tx_active = 0U;
        }
    }
}

/* Enable the USART1 interrupt. Call once after bsp_log_init(). */
void log_uart_init(void)
{
    uart_tx_enable_irq();
}

void vprintf_log(const char *fmt, va_list ap)
{
#if PRINT_LOG_ENABLE == 0
    (void)fmt; (void)ap;
    return;
#else
    char buf[LOG_BUF_SIZE];
    int32_t n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) return;
    if (n > (int32_t)sizeof(buf) - 1) n = (int32_t)sizeof(buf) - 1;
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

/* ---------------------------------------------------------------------------
 * Self-contained USART1 (PA9/PA10, 115200 8N1) initialization for 001.
 * No CubeMX main.h / g_uart_log.huart dependency.
 * ------------------------------------------------------------------------- */
void bsp_log_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Pin       = GPIO_PIN_9 | GPIO_PIN_10;   /* TX=PA9, RX=PA10 */
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &g);

    g_uart_log.huart.Instance          = USART1;
    g_uart_log.huart.Init.BaudRate     = 115200;
    g_uart_log.huart.Init.WordLength   = UART_WORDLENGTH_8B;
    g_uart_log.huart.Init.StopBits     = UART_STOPBITS_1;
    g_uart_log.huart.Init.Parity       = UART_PARITY_NONE;
    g_uart_log.huart.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    g_uart_log.huart.Init.Mode         = UART_MODE_TX_RX;
    g_uart_log.huart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&g_uart_log.huart) != HAL_OK)
    {
        return;   /* logging is best-effort; do not trap the boot */
    }

    uart_tx_enable_irq();   /* and keep the TXE interrupt off until first write */
}
