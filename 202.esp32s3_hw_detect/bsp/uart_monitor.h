#ifndef DEBUG_UART_MONITOR_H_
#define DEBUG_UART_MONITOR_H_

#pragma once
#include <Arduino.h>
#include "app/debug_module.h"
#include "app/ring_buffer.h"
#include "config/pin_config.h"

/**
 * @file uart_monitor.h
 * @brief UART debug port (target MCU log capture) built on HardwareSerial(1).
 *
 * RX bytes are drained into a lock-free ring buffer by a dedicated task, then
 * framed into DebugEvents (line based for text, fixed blocks for hex) and
 * pushed onto the EventBus. Baudrate / framing are reconfigurable at runtime.
 *
 * Pins come from config/pin_config.h and are hard-claimed via PinManager, so
 * no other module can steal GPIO17/18.
 */
namespace RHD {

class UartMonitor {
public:
    bool begin();

    // Runtime reconfiguration (Web "Config" page or `uart_config` command).
    void configure(unsigned long baud, uint8_t dataBits, uint8_t stopBits, uint8_t parity);

    // Transmit (echoed back as a UART_TX DebugEvent).
    void send(const String& text);

    bool          isReady() const    { return _ready; }
    uint32_t      dropCount() const  { return _dropped; }
    unsigned long baud() const       { return _baud; }

    void setHexMode(bool hex) { _hex = hex; }
    bool hexMode() const      { return _hex; }

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();
    void        frameAndPush();
    void        pushFrame(const char* text, size_t len, bool hex);
    static uint32_t buildConfig(uint8_t dataBits, uint8_t stopBits, uint8_t parity);

    HardwareSerial*  _serial = &Serial1;
    ByteRingBuffer*  _ring   = nullptr;
    bool             _ready   = false;
    bool             _hex     = false;
    unsigned long    _baud    = DebugPins::UART_DEFAULT_BAUDRATE;
    uint32_t         _dropped = 0;
    TaskHandle_t     _task    = nullptr;

    char      _line[AppConfig::EVENT_DATA_MAX];
    size_t    _lineLen  = 0;
    uint32_t  _lastByte = 0;
};

} // namespace RHD

#endif // DEBUG_UART_MONITOR_H_
