#ifndef CONFIG_MQTT_CONFIG_H_
#define CONFIG_MQTT_CONFIG_H_

#pragma once
#include <Arduino.h>

/**
 * @file mqtt_config.h
 * @brief MQTT topic layout and default broker settings.
 *
 * Topics are built as:  remote-debugger/<device_id>/<suffix>
 * The device_id is derived from the ESP32-S3 MAC at boot
 * (e.g. "esp32s3-A1B2C3D4") and is NOT hardcoded here.
 */
namespace MqttConfig {

// Topic suffixes
constexpr char TOPIC_STATUS[]   = "status";
constexpr char TOPIC_UART_RX[]  = "uart/rx";
constexpr char TOPIC_UART_TX[]  = "uart/tx";
constexpr char TOPIC_ADC_CH[]   = "adc/ch";   // + channel digit
constexpr char TOPIC_GPIO[]     = "gpio";
constexpr char TOPIC_EVENT[]    = "event";
constexpr char TOPIC_CMD[]      = "cmd";
constexpr char TOPIC_SYSTEM[]   = "system";

// Defaults (overridden by NVS at runtime)
constexpr char DEFAULT_BROKER[]   = "192.168.10.1";
constexpr int  DEFAULT_PORT       = 1883;   // 8883 for TLS
constexpr char DEFAULT_USERNAME[] = "";
constexpr char DEFAULT_PASSWORD[] = "";
constexpr char DEFAULT_CLIENT_PREFIX[] = "esp32s3-debugger";
constexpr int  DEFAULT_KEEPALIVE  = 30;     // seconds
constexpr bool DEFAULT_TLS        = false;

// Reconnect/backoff
constexpr uint32_t CONNECT_RETRY_MS   = 3000;
constexpr uint32_t PUBLISH_TIMEOUT_MS = 2000;
constexpr size_t   MAX_PAYLOAD_SIZE    = 384;

} // namespace MqttConfig

#endif // CONFIG_MQTT_CONFIG_H_
