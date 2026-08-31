#include "app/app.h"
#include "bsp/button.h"

/* Task_Button
 *   - Polls the button every BUTTON_POLL_MS (strict period).
 *   - On a debounced press it logs [BTN] pressed and sends EVENT_BUTTON_PRESSED
 *     through app_event_queue. Tasks never communicate via globals here. */

void task_button(void* pvParameters) {
    (void)pvParameters;

    button_init();
    log_info("Task_Button started");

    TickType_t last = xTaskGetTickCount();
    AppEvent ev = EVENT_BUTTON_PRESSED;

    for (;;) {
        if (button_poll_pressed()) {
            console_printf("[BTN] pressed\r\n");
            if (xQueueSend(app_event_queue, &ev,
                           pdMS_TO_TICKS(QUEUE_SEND_TIMEOUT_MS)) != pdTRUE) {
                log_err("Queue send failed (button)");
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}
