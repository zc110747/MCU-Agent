#ifndef APP_APP_H
#define APP_APP_H

#include <Arduino.h>
#include "config/config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/timers.h>

/* =========================================================================
 *  Application events passed through app_event_queue
 *  Produced by Task_Button and Task_UART, consumed by Task_LED.
 * ======================================================================= */
typedef enum {
    EVENT_BUTTON_PRESSED = 0,
    EVENT_LED_TOGGLE,
    EVENT_LED_ON,
    EVENT_LED_OFF,
} AppEvent;

/* Shared FreeRTOS objects (defined in app.cpp). */
extern QueueHandle_t     app_event_queue;   // button/uart -> led events
extern SemaphoreHandle_t sys_timer_sem;     // software timer -> monitor sync
extern TimerHandle_t     system_timer;      // 1 s heartbeat

/* Lifecycle. */
void app_init(void);    // create RTOS objects
void app_start(void);   // create tasks + timer, print banner

/* Console logging (mutex-protected). */
void console_lock(void);
void console_unlock(void);
void console_printf(const char* fmt, ...);  // locks, prints, unlocks
void log_info(const char* fmt, ...);        // [INFO] prefix
void log_err(const char* fmt, ...);         // [ERR] prefix

#endif /* APP_APP_H */
