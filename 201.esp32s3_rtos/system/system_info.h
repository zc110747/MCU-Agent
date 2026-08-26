#ifndef SYSTEM_SYSTEM_INFO_H
#define SYSTEM_SYSTEM_INFO_H

#include <Arduino.h>

/* Startup banner (chip / memory / FreeRTOS). */
void print_banner(void);

/* One-shot memory snapshot lines at startup. */
void print_mem_info(void);

/* Live FreeRTOS task table (name / state / priority / stack min / core). */
void print_task_list(void);

/* State enum -> short string (matches FreeRTOS eTaskState order). */
const char* task_state_name(int state);

/* Minimum-free tracking for heap & PSRAM (avoids relying on a specific
 * core version's getMinFreePsram() API). Call mem_update() periodically. */
void     mem_update(void);
uint32_t mem_min_heap(void);
uint32_t mem_min_psram(void);

#endif /* SYSTEM_SYSTEM_INFO_H */
