#include "app/app.h"

// Force compilation of module sources.
// The Arduino builder only compiles the .ino and top-level files; it does NOT
// auto-build .cpp files inside subfolders (app/, tasks/, bsp/, system/).
// Including them here compiles every module into the sketch translation unit,
// which works identically in the Arduino IDE and arduino-cli.
#include "config/config.h"
#include "bsp/led.cpp"
#include "bsp/button.cpp"
#include "system/system_info.cpp"
#include "app/app.cpp"
#include "tasks/task_led.cpp"
#include "tasks/task_button.cpp"
#include "tasks/task_monitor.cpp"
#include "tasks/task_uart.cpp"

void setup() {
    Serial.begin(UART_BAUD);
    delay(100);  // let the UART settle; setup-only, NOT a FreeRTOS task period

    log_info("System started");
    log_info("ESP32-S3 N16R8 detected");

    app_init();    // create queue / semaphore / mutex / timer
    app_start();   // create tasks + start timer + print banner
}

void loop() {
    // All application work runs inside FreeRTOS tasks created in setup().
    // Park the Arduino loop task so it does not spin.
    vTaskDelay(portMAX_DELAY);
}
