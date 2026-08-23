/* freertos_stub.h — minimal FreeRTOS symbols for offline SNMP unit tests. */
#ifndef __FREERTOS_STUB_H__
#define __FREERTOS_STUB_H__

#include <stdint.h>

#define portTICK_PERIOD_MS 1
#define pdPASS 1
#define pdFAIL 0
#define tskIDLE_PRIORITY 0
#define pdMS_TO_TICKS(x) (x)

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;

#define taskENTER_CRITICAL()  do{}while(0)
#define taskEXIT_CRITICAL()   do{}while(0)
#define vTaskDelay(x)         do{}while(0)

static TickType_t __tick = 0;
static inline TickType_t xTaskGetTickCount(void) { return __tick; }

#define xTaskGetSchedulerState() 0
#define taskSCHEDULER_RUNNING 1
#define xSemaphoreCreateMutex() ((void*)1)
#define xSemaphoreTake(a,b) 1
#define xSemaphoreGive(a) 1
#define xTaskCreate(a,b,c,d,e,f) 1
#define vTaskDelete(a) do{}while(0)

#endif /* __FREERTOS_STUB_H__ */
