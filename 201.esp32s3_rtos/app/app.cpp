#include "app.h"

#include <stdio.h>

#include "bsp/led.h"
#include "bsp/button.h"
#include "system/system_info.h"

#include "tasks/task_led.h"
#include "tasks/task_button.h"
#include "tasks/task_monitor.h"
#include "tasks/task_uart.h"

/* ---- FreeRTOS objects (defined here, referenced via app.h) ----------- */
QueueHandle_t     app_event_queue = NULL;
SemaphoreHandle_t sys_timer_sem   = NULL;
TimerHandle_t     system_timer    = NULL;

static SemaphoreHandle_t s_console_mutex = NULL;
static uint32_t         s_heartbeat     = 0;

/* ---- console mutex (priority-inheritance) ---------------------------- */
void console_lock(void) {
    if (s_console_mutex) {
        xSemaphoreTake(s_console_mutex, portMAX_DELAY);
    }
}
void console_unlock(void) {
    if (s_console_mutex) {
        xSemaphoreGive(s_console_mutex);
    }
}

/* ---- logging helpers -------------------------------------------------- */
static char s_log_buf[192];

void console_printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(s_log_buf, sizeof(s_log_buf), fmt, ap);
    va_end(ap);
    console_lock();
    Serial.print(s_log_buf);
    console_unlock();
}

void log_info(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(s_log_buf, sizeof(s_log_buf), fmt, ap);
    va_end(ap);
    console_lock();
    Serial.printf("[INFO] %s\r\n", s_log_buf);
    console_unlock();
}

void log_err(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(s_log_buf, sizeof(s_log_buf), fmt, ap);
    va_end(ap);
    console_lock();
    Serial.printf("[ERR] %s\r\n", s_log_buf);
    console_unlock();
}

/* ---- software timer callback (timer daemon task context) -------------- */
static void system_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    s_heartbeat++;
    if (sys_timer_sem) {
        xSemaphoreGive(sys_timer_sem);   // wake Task_Monitor once per second
    }
}

/* ---- object creation -------------------------------------------------- */
void app_init(void) {
    app_event_queue = xQueueCreate(APP_EVENT_QUEUE_LEN, sizeof(AppEvent));
    if (app_event_queue == NULL) {
        log_err("Failed to create app_event_queue");
    }

    sys_timer_sem = xSemaphoreCreateBinary();
    if (sys_timer_sem == NULL) {
        log_err("Failed to create sys_timer_sem");
    }

    s_console_mutex = xSemaphoreCreateMutex();
    if (s_console_mutex == NULL) {
        log_err("Failed to create console mutex");
    }

    system_timer = xTimerCreate(
        "SystemTimer",
        pdMS_TO_TICKS(1000),
        pdTRUE,                 // auto-reload -> 1 s heartbeat
        (void*)0,
        system_timer_cb);
    if (system_timer == NULL) {
        log_err("Failed to create SystemTimer");
    }
}

/* ---- task creation ---------------------------------------------------- */
void app_start(void) {
    if (app_event_queue == NULL || sys_timer_sem == NULL ||
        s_console_mutex == NULL || system_timer == NULL) {
        log_err("Critical init failure: cannot start tasks");
        return;
    }

    TaskHandle_t h = NULL;
    BaseType_t ok;

    ok = xTaskCreatePinnedToCore(task_led, "Task_LED", 2048, NULL,
                                 PRIO_LED, &h, CORE_APP_TASKS);
    if (ok != pdPASS) log_err("Failed to create Task_LED");

    ok = xTaskCreatePinnedToCore(task_button, "Task_Button", 2048, NULL,
                                 PRIO_BUTTON, &h, CORE_APP_TASKS);
    if (ok != pdPASS) log_err("Failed to create Task_Button");

    ok = xTaskCreatePinnedToCore(task_monitor, "Task_Monitor", 3072, NULL,
                                 PRIO_MONITOR, &h, CORE_APP_TASKS);
    if (ok != pdPASS) log_err("Failed to create Task_Monitor");

    ok = xTaskCreatePinnedToCore(task_uart, "Task_UART", 3072, NULL,
                                 PRIO_UART, &h, CORE_APP_TASKS);
    if (ok != pdPASS) log_err("Failed to create Task_UART");

    if (xTimerStart(system_timer, 0) != pdPASS) {
        log_err("Failed to start SystemTimer");
    }

    print_banner();
    print_mem_info();
}
