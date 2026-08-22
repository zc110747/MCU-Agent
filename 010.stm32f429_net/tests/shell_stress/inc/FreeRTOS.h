#ifndef INC_FREERTOS_H
#define INC_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
static inline void vTaskDelay(TickType_t) {}
#endif
