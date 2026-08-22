/*
 * FreeRTOSConfig.h - FreeRTOS V11.1.0 kernel configuration for STM32F429IGT6.
 *
 * Design notes:
 *  - Native FreeRTOS API (no CMSIS-RTOS wrapper).
 *  - SystemCoreClock = 180 MHz, tick 1 kHz.
 *  - configAPPLICATION_ALLOCATED_HEAP = 1: ucHeap is defined by the app with
 *    a section attribute so it lives in external SDRAM (0xC0000000), giving
 *    tasks/queues room to grow.  See app/sdram_heap.c + the linker script.
 *  - SysTick is owned by FreeRTOS; the HAL 1 ms time base is moved to TIM7
 *    (stm32f4xx_hal_timebase_tim.c).
 *  - Interrupt priorities: 0..15 (4 bits), kernel runs at 15, ISR-safe API
 *    calls allowed at priority >= 5.  The ETH IRQ uses priority 5 so
 *    tcpip_input() may be called from it.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx_hal.h"

/* ------------------------------------------------------------------ */
/* Application specific definitions                                    */
/* ------------------------------------------------------------------ */
#define configUSE_PREEMPTION                      1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   1
#define configUSE_TIME_SLICING                    1
#define configUSE_IDLE_HOOK                       0
#define configUSE_TICK_HOOK                       0
#define configUSE_TICKLESS_IDLE                   0

#define configCPU_CLOCK_HZ                        ( SystemCoreClock )
#define configTICK_RATE_HZ                        ( (TickType_t)1000 )

/* Tasks/queues/timers are allocated from ucHeap (SDRAM) via heap_4. */
#define configMAX_PRIORITIES                      ( 25 )
#define configMINIMAL_STACK_SIZE                  ( (uint16_t)128 )
#define configTOTAL_HEAP_SIZE                     ( (size_t)(512 * 1024) )
#define configMAX_TASK_NAME_LEN                   ( 16 )
#define configUSE_16_BIT_TICKS                    0
#define configIDLE_SHOULD_YIELD                   1

#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES             1
#define configUSE_TASK_NOTIFICATIONS              1
#define configUSE_QUEUE_SETS                      0
#define configQUEUE_REGISTRY_SIZE                 8

#define configUSE_TIMERS                          1
#define configTIMER_TASK_PRIORITY                 ( 4 )
#define configTIMER_QUEUE_LENGTH                  10
#define configTIMER_TASK_STACK_DEPTH              ( 256 )

#define configCHECK_FOR_STACK_OVERFLOW            2
#define configUSE_MALLOC_FAILED_HOOK              1
#define configUSE_DAEMON_TASK_STARTUP_HOOK        0

#define configUSE_TRACE_FACILITY                  1
#define configUSE_STATS_FORMATTING_FUNCTIONS      0
#define configUSE_CO_ROUTINES                     0
#define configMAX_CO_ROUTINE_PRIORITIES           ( 2 )
#define configUSE_NEWLIB_REENTRANT                0

#define configSUPPORT_STATIC_ALLOCATION           0
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configAPPLICATION_ALLOCATED_HEAP          1   /* ucHeap in SDRAM */

#define configUSE_POSIX_ERRNO                     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   0

/* V11: INCLUDE_* default to 0; enable what the app and LwIP use */
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_vTaskDelete                       1
#define INCLUDE_vTaskSuspend                      1
#define INCLUDE_vTaskResume                       1
#define INCLUDE_xTaskDelayUntil                   1
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_xTaskGetSchedulerState            1
#define INCLUDE_uxTaskPriorityGet                 1
#define INCLUDE_vTaskPrioritySet                  1
#define INCLUDE_xTaskResumeFromISR                1

/* Stack alignment + FPU: built with -mfloat-abi=soft, no FPU context save. */
#define configENABLE_FPU                          0
#define configENABLE_MPU                          0
#define configENABLE_TRUSTZONE                    0

/* ------------------------------------------------------------------ */
/* Interrupt nesting (STM32F4: 4 priority bits)                        */
/* ------------------------------------------------------------------ */
#define configPRIO_BITS                            4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY    15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* ------------------------------------------------------------------ */
/* Assert / diagnostics                                                */
/* ------------------------------------------------------------------ */
extern void vApplicationAssertFailed(const char *file, int line);
#define configASSERT(x)                                                     \
    if (!(x)) { vApplicationAssertFailed(__FILE__, __LINE__); }

#endif /* FREERTOS_CONFIG_H */
