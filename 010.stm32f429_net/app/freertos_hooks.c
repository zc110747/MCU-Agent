/**
  ******************************************************************************
  * @file    freertos_hooks.c
  * @brief   FreeRTOS callback hooks (assert / malloc failed / stack overflow).
  ******************************************************************************
  */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "log.h"

void vApplicationMallocFailedHook(void)
{
  PRINT_LOG("FreeRTOS: pvPortMalloc failed (heap exhausted)\r\n");
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  PRINT_LOG("FreeRTOS: stack overflow in task %s\r\n", pcTaskName);
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}

void vApplicationAssertFailed(const char *file, int line)
{
  PRINT_LOG("FreeRTOS assert failed: %s:%d\r\n", file, line);
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}
