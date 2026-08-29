/**
  ******************************************************************************
  * @file    bsp_uart.c
  * @brief   USART3 (PB10/PB11) debug console with a scheduler-aware TX path.
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
  *  Why the mutex is LAZY
  *  ---------------------
  *  BSP_UART_Init() is called before the SDRAM is up and therefore before
  *  vPortDefineHeapRegions(); xSemaphoreCreateMutex() there would allocate from
  *  an undefined heap.  The mutex is created on first use instead, i.e. on the
  *  first uart_write() that happens with the scheduler running - by then ucHeap
  *  is valid.  The critical section makes that lazy init race-free.
  ******************************************************************************
  */
#include "bsp_uart.h"
#include "FreeRTOS.h"   /* core FreeRTOS definitions */
#include "task.h"       /* taskENTER_CRITICAL / xTaskGetSchedulerState */
#include "semphr.h"
#include <string.h>

void Error_Handler(void);  /* defined in main.c */

UART_HandleTypeDef huart3;

/* ---- TX: ring buffer + critical section ---- */
static uint8_t g_tx_buf[UART_TX_BUF_SIZE];
static volatile uint16_t g_tx_head = 0;   /* written by uart_write */
static volatile uint16_t g_tx_tail = 0;   /* read by IRQ */
static volatile uint8_t  g_tx_busy = 0;   /* transmitter active */
static volatile uint8_t  g_uart_ready = 0;

/* Serialises concurrent uart_write() callers once the scheduler is up.
 * NULL until the first post-scheduler write (see the file header). */
static SemaphoreHandle_t g_tx_mutex = NULL;

static inline uint16_t tx_used(void)
{
  return (uint16_t)((g_tx_head - g_tx_tail) & (UART_TX_BUF_SIZE - 1));
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
  if (g_tx_mutex == NULL)
  {
    g_tx_mutex = xSemaphoreCreateMutex();
  }
  taskEXIT_CRITICAL();

  return g_tx_mutex;
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
  SemaphoreHandle_t mtx;

  /* Boot stage (or UART not up yet): the TXE ISR is not draining the ring, so
   * enqueue-only would silently swallow the boot banner.  Use a blocking
   * polled transmit instead - slow, but it never drops a byte and it does not
   * need the interrupt or the scheduler. */
  if ((g_uart_ready == 0) ||
      (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
  {
    if (g_uart_ready == 0)
    {
      return 0;                                   /* nothing to transmit on */
    }
    HAL_UART_Transmit(&huart3, (uint8_t *)data, (uint16_t)len, HAL_MAX_DELAY);
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
    uint16_t next = (uint16_t)((g_tx_head + 1) & (UART_TX_BUF_SIZE - 1));
    if (next == g_tx_tail) break;           /* full -> drop the remainder */
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
  uint32_t guard = 200000U;      /* ~1 s of spins: never hang the caller */

  /* Wait until no bytes are pending and the transmitter has gone idle.
   * Before the scheduler is up the polled path already sent everything, so
   * this returns immediately in that case. */
  while ((g_tx_busy != 0 || tx_used() != 0) && (guard-- > 0U))
  {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
      vTaskDelay(pdMS_TO_TICKS(1));   /* let the ISR make progress */
    }
  }
}

int uart_getchar_nowait(uint8_t *c)
{
  if (c == NULL)
  {
    return 0;
  }
  if ((huart3.Instance->SR & USART_SR_RXNE) == 0U)
  {
    return 0;   /* nothing waiting */
  }
  *c = (uint8_t)(huart3.Instance->DR & 0xFFU);
  return 1;
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

  /* RX is handled by polling uart_getchar_nowait() (which reads SR/DR
   * directly).  We deliberately do NOT consume RXNE here: the ISR is armed on
   * TXE, and while it was draining the TX ring it would otherwise read and
   * discard any pending RX byte, starving the polled reader.  RXNEIE is left
   * disabled, so this ISR only ever fires for TXE. */
}
