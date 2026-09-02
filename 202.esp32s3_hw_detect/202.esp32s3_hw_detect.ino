// ESP32-S3 Remote Hardware Debugger
// Entry point for the Arduino-CLI build. The Arduino builder only compiles the
// .ino and top-level files; it does NOT auto-build .cpp files inside subfolders
// (app/, debug/, network/, storage/, ota/). We #include every module source so
// the whole project becomes a single translation unit -- this matches how the
// Arduino IDE and arduino-cli behave, and keeps the modular layout on disk.
//
// Framework : Arduino Core for ESP32 (esp32:esp32:esp32s3)
// Build     : arduino-cli compile  (see build_oneclick.bat)
// Flash     : arduino-cli upload   (see flash-esp32.bat)

#include "app/debug_gateway.h"  // RHD::getGateway()

// ---- force compilation of all module sources ----
#include "app/event_bus.cpp"
#include "app/pin_manager.cpp"
#include "app/debug_gateway.cpp"

#include "debug/uart_monitor.cpp"
#include "debug/adc_monitor.cpp"
#include "debug/gpio_monitor.cpp"
#include "debug/ws2812_led.cpp"
#include "debug/pwm_output.cpp"

#include "network/wifi_manager.cpp"
#include "network/mqtt_manager.cpp"
#include "network/websocket_manager.cpp"
#include "network/web_server.cpp"

#include "storage/config_manager.cpp"
#include "storage/log_manager.cpp"

#include "ota/ota_manager.cpp"

void setup() {
    // All work is driven by FreeRTOS tasks created inside begin().
    RHD::getGateway().begin();
}

void loop() {
    // Event-driven system; the tasks do the real work.
    // Idle here so the Arduino loop task does not spin.
    delay(1000);
}
