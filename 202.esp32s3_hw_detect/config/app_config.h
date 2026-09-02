#ifndef CONFIG_APP_CONFIG_H_
#define CONFIG_APP_CONFIG_H_

#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

/**
 * @file app_config.h
 * @brief Firmware-wide constants and default runtime parameters.
 */
namespace AppConfig {

constexpr char FIRMWARE_VERSION[] = "1.0.0";
constexpr char PROJECT_NAME[]     = "ESP32-S3 Remote Hardware Debugger";

// --- Event bus ---
constexpr int   EVENT_QUEUE_LEN   = 64;    // FreeRTOS queue depth for DebugEvent
constexpr size_t EVENT_DATA_MAX   = 192;   // max payload bytes per DebugEvent
constexpr int   MQTT_QUEUE_LEN     = 64;   // internal queue for MQTT publishing

// --- Web / WebSocket ---
constexpr uint16_t HTTP_PORT      = 80;
constexpr uint16_t WEBSOCKET_PORT = 81;
constexpr uint16_t WEB_TASK_STACK = 8192;
constexpr uint8_t  WEB_TASK_PRIO  = 2;
constexpr BaseType_t WEB_TASK_CORE = 0;

// --- Tasks ---
constexpr uint16_t UART_TASK_STACK = 4096;
constexpr uint8_t  UART_TASK_PRIO  = 3;
constexpr BaseType_t UART_TASK_CORE = 1;

constexpr uint16_t ADC_TASK_STACK  = 3072;
constexpr uint8_t  ADC_TASK_PRIO   = 2;
constexpr BaseType_t ADC_TASK_CORE = 1;

constexpr uint16_t GPIO_TASK_STACK = 2048;
constexpr uint8_t  GPIO_TASK_PRIO  = 2;
constexpr BaseType_t GPIO_TASK_CORE = 1;

constexpr uint16_t LED_TASK_STACK  = 2048;
constexpr uint8_t  LED_TASK_PRIO   = 2;
constexpr BaseType_t LED_TASK_CORE  = 1;

constexpr uint16_t EVENT_TASK_STACK = 4096;
constexpr uint8_t  EVENT_TASK_PRIO  = 4;   // higher: keep up with producers
constexpr BaseType_t EVENT_TASK_CORE = 0;

constexpr uint16_t MQTT_TASK_STACK  = 5120;
constexpr uint8_t  MQTT_TASK_PRIO   = 3;
constexpr BaseType_t MQTT_TASK_CORE = 0;

// --- ADC sampling ---
constexpr uint32_t ADC_DEFAULT_INTERVAL_MS = 100;  // 10 Hz
constexpr int      ADC_CHANNEL_COUNT       = 4;

// --- Periodics ---
constexpr uint32_t SYSTEM_STATUS_INTERVAL_MS = 5000;  // publish status to WS/MQTT
constexpr uint32_t HEAP_PRINT_INTERVAL_MS    = 30000;

// --- Logging ---
constexpr int LOG_RING_LINES = 200;   // web "Logs" history

// --- Memory guard ---
constexpr uint32_t MIN_FREE_HEAP_WARN = 40000;  // warn if free heap below this

// --- WiFi ---
constexpr char AP_SSID_PREFIX[]   = "ESP32S3-Debugger-";  // + last 4 MAC hex
constexpr char AP_PASSWORD[]      = "debugger123";        // AP mode default password
const IPAddress AP_IP(192, 168, 4, 1);
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 5000;

// --- Web auth ---
constexpr char DEFAULT_WEB_PASSWORD[] = "admin";  // used for /config and /update

// NVS / Preferences namespace
constexpr char NVS_NAMESPACE[] = "rhd_cfg";

} // namespace AppConfig

#endif // CONFIG_APP_CONFIG_H_
