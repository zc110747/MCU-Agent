#ifndef APP_DEBUG_MODULE_H_
#define APP_DEBUG_MODULE_H_

#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config/app_config.h"

/**
 * @file debug_module.h
 * @brief Core event types and the DebugModule extension interface.
 *
 * Every piece of debug data (UART / ADC / GPIO / system) first becomes a
 * DebugEvent and is pushed onto the EventBus (a FreeRTOS queue). Consumers
 * (WebSocket / MQTT / Log) read from the bus and never talk to producers
 * directly. New modules (CAN, I2C, SPI, Logic Analyzer...) only need to
 * push DebugEvents to participate in the whole pipeline.
 */
namespace RHD {

enum class DebugEventType : uint8_t {
    UART_RX,
    UART_TX,
    ADC_SAMPLE,
    GPIO_STATE,
    LED_STATE,
    PWM_STATE,
    SYSTEM,
    ERROR
};

// Packed event. Numeric fields are typed so consumers do not have to parse
// the raw `data` blob. `data` is used for UART text/hex payloads.
struct DebugEvent {
    DebugEventType type      = DebugEventType::SYSTEM;
    uint32_t       timestamp = 0;
    uint8_t        channel   = 0;     // ADC channel
    uint8_t        gpio      = 0;     // GPIO number
    uint8_t        state     = 0;     // GPIO state (0/1)
    uint16_t       raw       = 0;     // ADC raw value
    float          voltage   = 0.0f;  // ADC computed voltage
    uint8_t        encoding  = 0;     // UART payload encoding: 0=text, 1=hex
    uint16_t       length    = 0;     // bytes used in `data`
    uint8_t        data[AppConfig::EVENT_DATA_MAX];
};

/**
 * @brief Extension interface for future debug modules.
 *
 * New hardware interfaces (CAN, I2C, SPI, Power Monitor, Logic Analyzer...)
 * implement this interface and are driven by the gateway without any change
 * to MQTT / Web / EventBus core code.
 */
class DebugModule {
public:
    virtual ~DebugModule() = default;
    virtual bool begin() = 0;
    virtual void update() = 0;
    virtual const char* name() const = 0;
};

} // namespace RHD

#endif // APP_DEBUG_MODULE_H_
