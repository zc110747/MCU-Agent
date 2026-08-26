#include "bsp_uart.h"
#include "FreeRTOS.h"   /* core FreeRTOS definitions */
#include "task.h"      /* taskENTER_CRITICAL / taskEXIT_CRITICAL (via portmacro.h) */
#include <string.h>

void Error_Handler(void);  /* defined in main.c */

UART_HandleTypeDef huart3;

/* ---- TX: ring buffer + critical section (NO FreeRTOS object) ---- */
static uint8_t g_tx_buf[UART_TX_BUF_SIZE];
static volatile uint16_t g_tx_head = 0;   /* written by uart_write */
static volatile uint16_t g_tx_tail = 0;   /* read by IRQ */
static volatile uint8_t  g_tx_busy = 0;   /* transmitter active */
static volatile uint8_t  g_uart_ready = 0;

static inline uint16_t tx_free(void)
{
  return (uint16_t)(UART_TX_BUF_SIZE - 1 -
         ((g_tx_head - g_tx_tail) & (UART_TX_BUF_SIZE - 1)));
}

void BSP_UART_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_USART3_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  GPIO_InitStruct.Pin       = GPIO_PIN_10 | GPIO_PIN_11;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  huart3.Instance          = USART3;
  huart3.Init.BaudRate     = 115200;
  huart3.Init.WordLength   = UART_WORDLENGTH_8B;
  huart3.Init.StopBits     = UART_STOPBITS_1;
  huart3.Init.Parity       = UART_PARITY_NONE;
  huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart3.Init.Mode         = UART_MODE_TX_RX;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }

  /* Enable TXE interrupt so the ring drains in the ISR.  Priority below
   * configMAX_SYSCALL_INTERRUPT_PRIORITY is not required for TX (no FreeRTOS
   * API used), but keep it modest. */
  HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(USART3_IRQn);
  g_uart_ready = 1;
}

int uart_write(const uint8_t *data, int len)
{
  int queued = 0;

  /* Before the UART IRQ is live (g_uart_ready==0) or as a belt-and-
   * suspenders fallback, use a blocking polled transmit.  This is what the
   * boot log uses before the scheduler starts. */
  if (g_uart_ready == 0)
  {
    HAL_UART_Transmit(&huart3, (uint8_t *)data, (uint16_t)len, HAL_MAX_DELAY);
    return len;
  }

  /* Critical section protects the ring counters + busy flag from the ISR.
   * It is safe both before and after the scheduler is running. */
  taskENTER_CRITICAL();
  for (int i = 0; i < len; i++)
  {
    uint16_t next = (uint16_t)((g_tx_head + 1) & (UART_TX_BUF_SIZE - 1));
    if (next == g_tx_tail) break;           /* full */
    g_tx_buf[g_tx_head] = data[i];
    g_tx_head = next;
    queued++;
  }
  if (g_tx_busy == 0 && g_tx_head != g_tx_tail)
  {
    g_tx_busy = 1;
    SET_BIT(huart3.Instance->CR1, USART_CR1_TXEIE);
  }
  taskEXIT_CRITICAL();

  return queued;
}

int uart_puts(const char *s)
{
  return uart_write((const uint8_t *)s, (int)strlen(s));
}

/* Real USART3 vector: overrides the weak Default_Handler in the startup .s. */
void USART3_IRQHandler(void)
{
  BSP_UART_IRQHandler();
}

void BSP_UART_IRQHandler(void)
{
  uint32_t sr = READ_REG(huart3.Instance->SR);

  if ((sr & USART_SR_TXE) != 0)
  {
    if (g_tx_tail != g_tx_head)
    {
      huart3.Instance->DR = g_tx_buf[g_tx_tail];
      g_tx_tail = (uint16_t)((g_tx_tail + 1) & (UART_TX_BUF_SIZE - 1));
    }
    else
    {
      CLEAR_BIT(huart3.Instance->CR1, USART_CR1_TXEIE);
      g_tx_busy = 0;
    }
  }

  /* RX: clear RXNE to avoid sticky interrupt; not consumed by the demo. */
  if ((sr & USART_SR_RXNE) != 0)
  {
    (void)huart3.Instance->DR;
  }
}
