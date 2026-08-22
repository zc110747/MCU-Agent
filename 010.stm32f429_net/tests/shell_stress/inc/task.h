#ifndef INC_TASK_H
#define INC_TASK_H
#include <stdint.h>
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define pdPASS 1
static inline int xTaskCreate(void (*fn)(void*), const char *name,
                              uint16_t stack, void *arg, unsigned prio,
                              void **handle) { (void)fn;(void)name;(void)stack;(void)arg;(void)prio;(void)handle; return pdPASS; }
static inline unsigned xTaskGetSchedulerState(void) { return 0; }
#endif
