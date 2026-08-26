#include "app/app.h"
#include "system/system_info.h"

/* Task_Monitor
 *   - Woken once per second by the Software Timer (via sys_timer_sem).
 *     The timer callback only gives the semaphore, so it performs no
 *     blocking I/O and no heavy Serial output.
 *   - Reports uptime, heap, PSRAM and the FreeRTOS task table.
 *   - Lowest priority: it never starves interactive I/O tasks. */

void task_monitor(void* pvParameters) {
    (void)pvParameters;

    log_info("Task_Monitor started");

    for (;;) {
        /* Wait for the 1 s heartbeat. Fallback timeout keeps reporting even
         * if the timer somehow stopped. */
        if (xSemaphoreTake(sys_timer_sem,
                           pdMS_TO_TICKS(MONITOR_PERIOD_MS * 2)) != pdTRUE) {
            // timeout: continue and report anyway
        }

        mem_update();
        uint32_t up = millis() / 1000;

        console_printf("[SYS] uptime: %us\r\n", up);
        console_printf("[SYS] free_heap: %u KB\r\n",   (unsigned)(ESP.getFreeHeap()  / 1024));
        console_printf("[SYS] min_free_heap: %u KB\r\n",(unsigned)(mem_min_heap()   / 1024));
        console_printf("[SYS] free_psram: %u KB\r\n",  (unsigned)(ESP.getFreePsram() / 1024));
        console_printf("[SYS] min_free_psram: %u KB\r\n",(unsigned)(mem_min_psram()/ 1024));
        /* ESP32 Arduino Core has no reliable per-core CPU-load API; we do not
         * fabricate usage numbers. We instead report core availability and the
         * actual Core ID of each task in the task table below. */
        console_printf("[SYS] CPU0: available\r\n");
        console_printf("[SYS] CPU1: available\r\n");
        console_printf("[MEM] heap=%uKB min=%uKB\r\n",
                       (unsigned)(ESP.getFreeHeap() / 1024),
                       (unsigned)(mem_min_heap()    / 1024));

        print_task_list();
    }
}
