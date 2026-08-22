/**
  ******************************************************************************
  * @file    freertos_hooks.c
  * @brief   FreeRTOS callback hooks (assert / malloc failed / stack overflow).
  ******************************************************************************
  */
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

void vApplicationMallocFailedHook(void)
{
  printf("FreeRTOS: pvPortMalloc failed (heap exhausted)\r\n");
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  printf("FreeRTOS: stack overflow in task %s\r\n", pcTaskName);
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}

void vApplicationAssertFailed(const char *file, int line)
{
  printf("FreeRTOS assert failed: %s:%d\r\n", file, line);
  taskDISABLE_INTERRUPTS();
  for (;;) { }
}
