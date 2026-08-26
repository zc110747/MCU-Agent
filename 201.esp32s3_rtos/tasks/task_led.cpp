#include "app/app.h"
#include "drivers/led.h"

/* Task_LED
 *   - Heartbeat: toggles the LED every LED_PERIOD_MS (strict period).
 *   - Owns the LED: consumes AppEvents from app_event_queue so that button
 *     presses and UART "led ..." commands are applied through one owner,
 *     never via shared global variables. */

static bool s_blink = true;   // true = heartbeat blink; false = forced on/off

void task_led(void* pvParameters) {
    (void)pvParameters;

    led_init();
    log_info("Task_LED started");

    TickType_t last = xTaskGetTickCount();
    AppEvent ev;
    bool toggled = false;

    for (;;) {
        toggled = false;
        /* Drain any pending events (non-blocking). */
        while (xQueueReceive(app_event_queue, &ev, 0) == pdTRUE) {
            switch (ev) {
                case EVENT_BUTTON_PRESSED:
                    led_toggle();           // visible feedback for the press
                    toggled = true;
                    break;
                case EVENT_LED_TOGGLE:
                    s_blink = true;
                    led_toggle();
                    toggled = true;
                    break;
                case EVENT_LED_ON:
                    s_blink = false;
                    led_set(true);
                    break;
                case EVENT_LED_OFF:
                    s_blink = false;
                    led_set(false);
                    break;
                default:
                    break;
            }
        }

        /* Strict-period heartbeat. Skip this iteration's heartbeat toggle if a
         * discrete event already toggled the LED, otherwise a press / "led
         * toggle" would toggle twice and cancel out. */
        if (s_blink && !toggled) {
            led_toggle();
        }

        vTaskDelayUntil(&last, pdMS_TO_TICKS(LED_PERIOD_MS));
    }
}
