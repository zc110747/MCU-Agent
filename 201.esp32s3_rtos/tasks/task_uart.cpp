#include "app/app.h"
#include "bsp/led.h"
#include "system/system_info.h"

/* Task_UART
 *   - Polls UART0 (Serial) every UART_POLL_MS, assembles a line, and runs a
 *     simple command interpreter.
 *   - "led on/off/toggle" are routed to Task_LED through app_event_queue so
 *     the LED driver still has a single owner. */

static String s_line;

static void handle_command(const String& raw) {
    String cmd = raw;
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "help") {
        console_printf("Commands:\r\n");
        console_printf("  help\r\n");
        console_printf("  status\r\n");
        console_printf("  led on\r\n");
        console_printf("  led off\r\n");
        console_printf("  led toggle\r\n");
        console_printf("  heap\r\n");
        console_printf("  psram\r\n");
        console_printf("  tasks\r\n");
    } else if (cmd == "status") {
        console_printf("Uptime: %us\r\n", millis() / 1000);
        console_printf("Free Heap: %u KB\r\n",  (unsigned)(ESP.getFreeHeap()  / 1024));
        console_printf("Free PSRAM: %u KB\r\n", (unsigned)(ESP.getFreePsram() / 1024));
    } else if (cmd == "led on") {
        AppEvent ev = EVENT_LED_ON;
        xQueueSend(app_event_queue, &ev, 0);
        console_printf("LED ON\r\n");
    } else if (cmd == "led off") {
        AppEvent ev = EVENT_LED_OFF;
        xQueueSend(app_event_queue, &ev, 0);
        console_printf("LED OFF\r\n");
    } else if (cmd == "led toggle") {
        AppEvent ev = EVENT_LED_TOGGLE;
        xQueueSend(app_event_queue, &ev, 0);
        console_printf("LED TOGGLE\r\n");
    } else if (cmd == "heap") {
        console_printf("Heap:\r\n");
        console_printf("  Total: %u KB\r\n",        (unsigned)(ESP.getHeapSize()   / 1024));
        console_printf("  Free: %u KB\r\n",         (unsigned)(ESP.getFreeHeap()   / 1024));
        console_printf("  Minimum Free: %u KB\r\n",(unsigned)(mem_min_heap()      / 1024));
    } else if (cmd == "psram") {
        if (psramFound()) {
            console_printf("PSRAM:\r\n");
            console_printf("  Total: %u KB\r\n",        (unsigned)(ESP.getPsramSize()  / 1024));
            console_printf("  Free: %u KB\r\n",         (unsigned)(ESP.getFreePsram()  / 1024));
            console_printf("  Minimum Free: %u KB\r\n", (unsigned)(mem_min_psram()     / 1024));
        } else {
            console_printf("PSRAM: not available\r\n");
        }
    } else if (cmd == "tasks") {
        print_task_list();
    } else if (cmd.length() > 0) {
        console_printf("Unknown command: %s\r\n", raw.c_str());
    }
}

void task_uart(void* pvParameters) {
    (void)pvParameters;

    log_info("Task_UART started");
    s_line.reserve(80);

    TickType_t last = xTaskGetTickCount();

    for (;;) {
        while (Serial.available() > 0) {
            int c = Serial.read();
            if (c == '\n' || c == '\r') {
                if (s_line.length() > 0) {
                    console_printf("> %s\r\n", s_line.c_str());
                    handle_command(s_line);
                    s_line = "";
                }
            } else if (c >= 32 && c < 127) {
                if (s_line.length() < 128) {
                    s_line += (char)c;
                }
            }
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(UART_POLL_MS));
    }
}
