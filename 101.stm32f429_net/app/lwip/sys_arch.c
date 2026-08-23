/*
 * sys_arch.c - LwIP OS abstraction layer for FreeRTOS (V11).
 *
 * Implements the sys_* API on top of FreeRTOS primitives:
 *   - mbox  -> FreeRTOS queue of void* (messages are pointers)
 *   - sem   -> binary semaphore (count=1) with ISR-safe signal
 *   - mutex -> recursive mutex
 *   - thread-> xTaskCreate (native task, stack from ucHeap in SDRAM)
 *   - now/jiffies -> FreeRTOS tick
 *   - protect -> vPortEnterCritical / vPortExitCritical
 */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "arch/sys_arch.h"

#include <string.h>

/* ---- critical sections ---- */
sys_prot_t sys_arch_protect(void)
{
  /* LwIP calls SYS_ARCH_PROTECT from the ETH ISR too (memp pool alloc in
   * HAL_ETH_RxAllocateCallback).  vPortEnterCritical() is not ISR-safe
   * (port.c asserts), and an ISR is already atomic w.r.t. tasks, so skip
   * the critical section there. */
  if (!xPortIsInsideInterrupt())
  {
    vPortEnterCritical();
  }
  return 0;
}

void sys_arch_unprotect(sys_prot_t pval)
{
  (void)pval;
  if (!xPortIsInsideInterrupt())
  {
    vPortExitCritical();
  }
}

/* ---- mailboxes (FreeRTOS queues holding void* messages) ---- */
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
  *mbox = xQueueCreate((UBaseType_t)size, sizeof(void *));
  return (*mbox == NULL) ? ERR_MEM : ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
  if (*mbox != NULL)
  {
    vQueueDelete(*mbox);
    *mbox = NULL;
  }
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
  BaseType_t ret = xQueueSend(*mbox, &msg, portMAX_DELAY);
  LWIP_ASSERT("mbox full", ret == pdTRUE);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
  /* tcpip_input() may be called from the ETH IRQ: use the FromISR path then */
  if (xPortIsInsideInterrupt())
  {
    BaseType_t higher = pdFALSE;
    BaseType_t ret = xQueueSendFromISR(*mbox, &msg, &higher);
    if (ret == pdTRUE)
    {
      portYIELD_FROM_ISR(higher);
      return ERR_OK;
    }
    return ERR_MEM;
  }
  return (xQueueSend(*mbox, &msg, 0U) == pdTRUE) ? ERR_OK : ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
  BaseType_t higher = pdFALSE;
  BaseType_t ret = xQueueSendFromISR(*mbox, &msg, &higher);
  if (ret == pdTRUE)
  {
    portYIELD_FROM_ISR(higher);
    return ERR_OK;
  }
  return ERR_MEM;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
  BaseType_t ret;
  TickType_t wait = (timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);

  if (msg != NULL)
  {
    *msg = NULL;
  }
  ret = xQueueReceive(*mbox, msg, wait);
  return (ret == pdTRUE) ? 0U : SYS_ARCH_TIMEOUT;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
  BaseType_t ret = xQueueReceive(*mbox, msg, 0U);
  return (ret == pdTRUE) ? 0U : SYS_ARCH_TIMEOUT;
}

/* ---- semaphores (binary) ---- */
err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
  *sem = xSemaphoreCreateBinary();
  if (*sem == NULL)
  {
    return ERR_MEM;
  }
  if (count != 0)
  {
    xSemaphoreGive(*sem);   /* start taken */
  }
  return ERR_OK;
}

void sys_sem_free(sys_sem_t *sem)
{
  if (*sem != NULL)
  {
    vSemaphoreDelete(*sem);
    *sem = NULL;
  }
}

void sys_sem_signal(sys_sem_t *sem)
{
  if (xPortIsInsideInterrupt())
  {
    BaseType_t higher = pdFALSE;
    xSemaphoreGiveFromISR(*sem, &higher);
    portYIELD_FROM_ISR(higher);
  }
  else
  {
    xSemaphoreGive(*sem);
  }
}

void sys_sem_signal_isr(sys_sem_t *sem)
{
  BaseType_t higher = pdFALSE;
  xSemaphoreGiveFromISR(*sem, &higher);
  portYIELD_FROM_ISR(higher);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
  BaseType_t ret;
  TickType_t wait = (timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);

  ret = xSemaphoreTake(*sem, wait);
  return (ret == pdTRUE) ? 0U : SYS_ARCH_TIMEOUT;
}

/* ---- mutexes (recursive) ---- */
err_t sys_mutex_new(sys_mutex_t *mutex)
{
  *mutex = xSemaphoreCreateRecursiveMutex();
  return (*mutex == NULL) ? ERR_MEM : ERR_OK;
}

void sys_mutex_free(sys_mutex_t *mutex)
{
  if (*mutex != NULL)
  {
    vSemaphoreDelete(*mutex);
    *mutex = NULL;
  }
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
  xSemaphoreTakeRecursive(*mutex, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
  xSemaphoreGiveRecursive(*mutex);
}

/* ---- threads ---- */
sys_thread_t sys_thread_new(const char *name, void (*thread)(void *arg),
                            void *arg, int stacksize, int prio)
{
  TaskHandle_t h = NULL;

  if (xTaskCreate(thread, name, (configSTACK_DEPTH_TYPE)stacksize, arg,
                  (UBaseType_t)prio, &h) != pdPASS)
  {
    return NULL;
  }
  return h;
}

/* ---- time ---- */
u32_t sys_now(void)
{
  return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

u32_t sys_jiffies(void)
{
  return (u32_t)xTaskGetTickCount();
}

void sys_msleep(u32_t ms)
{
  vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ---- init (no-op: FreeRTOS already running when LwIP starts) ---- */
void sys_init(void)
{
}
