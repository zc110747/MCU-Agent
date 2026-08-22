#include "bsp_uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

/* Defined in main.c */
void Error_Handler(void);

UART_HandleTypeDef huart1;

/* ---- RX: FreeRTOS queue (ISR producer, shell_task consumer).
 * Eliminates the previous ISR/task shared ring counters (head/tail) that
 * could be observed in inconsistent state when a burst of bytes arrived
 * while the shell was just entering/exiting uart_getc. ---- */
static QueueHandle_t s_rx_queue = NULL;

/* ---- TX: ring buffer + critical section ---- */
static uint8_t g_tx_buf[UART_TX_BUF_SIZE];
static volatile uint16_t g_tx_head = 0;   /* written by uart_write */
static volatile uint16_t g_tx_tail = 0;   /* read by IRQ */
static volatile uint8_t  g_tx_busy = 0;   /* transmitter active */

/* Mutex protects the TX ring enqueue so multiple threads can call uart_puts */
static SemaphoreHandle_t g_tx_mutex = NULL;

static inline uint16_t tx_free(void)
{
  return (uint16_t)(UART_TX_BUF_SIZE - 1 -
         ((g_tx_head - g_tx_tail) & (UART_TX_BUF_SIZE - 1)));
}

void BSP_UART_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitStruct.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
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

  if (g_tx_mutex == NULL)
  {
    g_tx_mutex = xSemaphoreCreateMutex();
  }
  if (s_rx_queue == NULL)
  {
    s_rx_queue = xQueueCreate(UART_RX_BUF_SIZE, sizeof(uint8_t));
    if (s_rx_queue == NULL) Error_Handler();
  }

  /* Enable RX interrupt (CR1.RXNEIE). Use the register directly to avoid any
   * ambiguity in the HAL interrupt-mask encoding. */
  SET_BIT(huart1.Instance->CR1, USART_CR1_RXNEIE);
  /* Set the IRQ priority below configMAX_SYSCALL_INTERRUPT_PRIORITY so the
   * FreeRTOS-aware HAL can use FromISR primitives if needed. */
  HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
}

int uart_write(const uint8_t *data, int len)
{
  int queued = 0;

  /* Before the scheduler is up, _write() (printf) may be called for the boot
   * log. Taking a mutex with portMAX_DELAY would deadlock / assert, so fall
   * back to a simple blocking polled transmit. */
  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING || g_tx_mutex == NULL)
  {
    HAL_UART_Transmit(&huart1, (uint8_t *)data, (uint16_t)len, HAL_MAX_DELAY);
    return len;
  }

  if (xSemaphoreTake(g_tx_mutex, portMAX_DELAY) != pdTRUE) return 0;

  /* Enter critical section: protect the ring counters, the busy flag, and
   * the TXEIE bit from being concurrently touched by the USART1 ISR. The
   * critical region is tiny (a few register moves) so it does not block
   * other interrupts meaningfully. */
  taskENTER_CRITICAL();
  for (int i = 0; i < len; i++)
  {
    uint16_t next = (uint16_t)((g_tx_head + 1) & (UART_TX_BUF_SIZE - 1));
    if (next == g_tx_tail) break;           /* full */
    g_tx_buf[g_tx_head] = data[i];
    g_tx_head = next;
    queued++;
  }
  /* Start transmitter if idle */
  if (g_tx_busy == 0 && g_tx_head != g_tx_tail)
  {
    g_tx_busy = 1;
    SET_BIT(huart1.Instance->CR1, USART_CR1_TXEIE);
  }
  taskEXIT_CRITICAL();

  xSemaphoreGive(g_tx_mutex);
  return queued;
}

int uart_puts(const char *s)
{
  return uart_write((const uint8_t *)s, (int)strlen(s));
}

int uart_getc(uint8_t *c)
{
  if (s_rx_queue == NULL) return 0;
  /* Non-blocking: timeout 0 returns immediately if the queue is empty. */
  return (xQueueReceive(s_rx_queue, c, 0) == pdTRUE) ? 1 : 0;
}

/* Block until the TX ring is fully drained by the USART1 ISR (transmitter
 * idle). Used before a reset/reboot so the last message reaches the wire.
 * Safe before the scheduler is up (just spins on the ring state). */
void uart_flush(void)
{
  /* Wait until no bytes are pending and the transmitter has gone idle. */
  while (g_tx_busy != 0 || g_tx_head != g_tx_tail)
  {
    /* If the scheduler is running, let the ISR make progress. */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
      vTaskDelay(pdMS_TO_TICKS(1));
  }
}

/* Real USART1 vector: overrides the weak Default_Handler in startup_stm32f429xx.s.
 * The startup file does NOT call BSP_UART_IRQHandler, so we must provide the
 * actual interrupt entry here and forward to the shared handler. */
void USART1_IRQHandler(void)
{
  BSP_UART_IRQHandler();
}

void BSP_UART_IRQHandler(void)
{
  uint32_t sr = READ_REG(huart1.Instance->SR);
  BaseType_t xWoken = pdFALSE;

  /* RX: hand the byte to the FreeRTOS queue. The queue is the sole owner of
   * buffering, so the ISR never races with shell_task on head/tail.
   * Before the scheduler is running we must NOT touch the queue (no task
   * owns it yet, and xQueueSendFromISR before vTaskStartScheduler is unsafe),
   * so just drain DR to clear RXNE and discard the byte.
   *
   * IMPORTANT: do NOT call portYIELD_FROM_ISR() inside the RX branch. That
   * macro performs a plain store to the NVIC INT_CTRL "PendSV set" register
   * with no dsb/isb barrier. Combined with the DR read that clears RXNE, it
   * can re-trigger / re-sample the RXNE pending state on ISR exit and cause
   * the SAME byte to be enqueued twice (seen as duplicated/garbled input such
   * as "he" -> "hhelphehe"). The RX path does not need a yield at all (no
   * higher-priority task is being unblocked), so we only accumulate the flag
   * and yield once, at the very end, after a dsb(). */
  if ((sr & USART_SR_RXNE) != 0)
  {
    uint8_t b = (uint8_t)(huart1.Instance->DR & 0xFF);
    /* Ensure RXNE is cleared in the peripheral before we leave (barrier). */
    __DSB();
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && s_rx_queue != NULL)
    {
      (void)xQueueSendFromISR(s_rx_queue, &b, &xWoken);
    }
    /* else: pre-scheduler, discard (RXNE already cleared by reading DR). */
  }

  /* TX: drain the ring buffer until empty, then stop the TXE interrupt.
   * Head/tail are volatile; the producer (uart_write) only mutates them
   * inside taskENTER_CRITICAL, so the snapshot we read here is consistent. */
  if ((sr & USART_SR_TXE) != 0)
  {
    if (g_tx_tail != g_tx_head)
    {
      huart1.Instance->DR = g_tx_buf[g_tx_tail];
      g_tx_tail = (uint16_t)((g_tx_tail + 1) & (UART_TX_BUF_SIZE - 1));
    }
    else
    {
      /* Nothing left: stop TX interrupt, allow further IDLE/TC handling. */
      CLEAR_BIT(huart1.Instance->CR1, USART_CR1_TXEIE);
      g_tx_busy = 0;
    }
  }

  /* Single yield at the end, after all peripheral accesses are complete and
   * the DSB above guarantees RXNE is cleared. TX completion may unblock a
   * task waiting on the ring, so keep the yield for that path only. */
  portYIELD_FROM_ISR(xWoken);
}

