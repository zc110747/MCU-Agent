/**
  ******************************************************************************
  * @file    bsp_console.c
  * @brief   Unified UART + USB CDC console - see bsp_console.h.
  *
  *  RX path
  *  -------
  *  USART1 runs in interrupt mode (RXNE only) and pushes straight into a
  *  256 byte ring.  The USB side is polled from bsp_console_task() because
  *  tinyusb already buffers a full endpoint worth of data for us.  Both feed
  *  the same ring, so the command parser upstream never has to know which
  *  cable the operator is using.
  *
  *  TX path
  *  -------
  *  UART transmit is blocking with a short timeout; the USB write is a
  *  best-effort push into the CDC FIFO and is silently dropped when no host
  *  is listening.  A disconnected USB cable must never stall the firmware,
  *  which is exactly what would happen if we waited for the FIFO to drain.
  ******************************************************************************
  */
#include "bsp_console.h"
#include "drv_usb_cdc.h"
#include "main.h"

#include <string.h>

#define CON_RX_LEN      256U            /* power of two */
#define CON_RX_MASK     (CON_RX_LEN - 1U)
#define CON_TX_TIMEOUT  20U             /* ms, UART blocking transmit */

static volatile uint8_t  s_rx[CON_RX_LEN];
static volatile uint8_t  s_rx_src[CON_RX_LEN];
static volatile uint32_t s_rx_head;
static volatile uint32_t s_rx_tail;
static volatile uint8_t  s_last_src = (uint8_t)CONSOLE_SRC_NONE;

void bsp_console_rx_push(uint8_t byte, console_src_t src)
{
    uint32_t next = (s_rx_head + 1U) & CON_RX_MASK;

    if (next == s_rx_tail)
    {
        return;                         /* ring full - drop, never block */
    }

    s_rx[s_rx_head]     = byte;
    s_rx_src[s_rx_head] = (uint8_t)src;
    s_rx_head           = next;
}

void bsp_console_init(void)
{
    s_rx_head  = 0U;
    s_rx_tail  = 0U;
    s_last_src = (uint8_t)CONSOLE_SRC_NONE;

    /* RXNE interrupt only: TX stays polled, so no TXE storm to service. */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);

    HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

void bsp_console_uart_irq(void)
{
    uint32_t isr = huart1.Instance->ISR;

    if ((isr & USART_ISR_RXNE_RXFNE) != 0U)
    {
        uint8_t byte = (uint8_t)(huart1.Instance->RDR & 0xFFU);
        bsp_console_rx_push(byte, CONSOLE_SRC_UART);
    }

    /* Overrun / framing / noise: clearing them is mandatory, otherwise RXNE
     * never fires again and the console goes deaf after the first glitch. */
    if ((isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0U)
    {
        huart1.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF |
                               USART_ICR_NECF  | USART_ICR_PECF;
    }
}

void bsp_console_task(void)
{
    uint8_t  buf[64];
    uint32_t n;
    uint32_t i;

    drv_usb_cdc_task();

    n = drv_usb_cdc_read(buf, sizeof(buf));
    for (i = 0U; i < n; i++)
    {
        bsp_console_rx_push(buf[i], CONSOLE_SRC_USB);
    }
}

int bsp_console_getc(void)
{
    int byte;

    if (s_rx_tail == s_rx_head)
    {
        return -1;
    }

    byte       = (int)s_rx[s_rx_tail];
    s_last_src = s_rx_src[s_rx_tail];
    s_rx_tail  = (s_rx_tail + 1U) & CON_RX_MASK;

    return byte;
}

console_src_t bsp_console_last_source(void)
{
    return (console_src_t)s_last_src;
}

void bsp_console_write(const char *data, uint32_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    (void)HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len,
                            CON_TX_TIMEOUT);

    (void)drv_usb_cdc_write(data, len);
}

void bsp_console_puts(const char *text)
{
    if (text != NULL)
    {
        bsp_console_write(text, (uint32_t)strlen(text));
    }
}

int bsp_console_usb_ready(void)
{
    return drv_usb_cdc_connected();
}

/**
  * @brief  printf() sink.  Strong override of the weak stub in syscalls.c so
  *         every diagnostic message reaches both cables.
  */
int _write(int file, char *ptr, int len)
{
    (void)file;

    if ((ptr == NULL) || (len <= 0))
    {
        return 0;
    }

    bsp_console_write(ptr, (uint32_t)len);

    return len;
}
