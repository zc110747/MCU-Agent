#ifndef DEBUG_GPIO_MONITOR_H_
#define DEBUG_GPIO_MONITOR_H_

#pragma once
#include <Arduino.h>
#include "app/debug_module.h"
#include "config/pin_config.h"

/**
 * @file gpio_monitor.h
 * @brief 4-channel digital monitor (GPIO4..7 by default).
 *
 * All channels start as INPUT_PULLUP. `setPin()` promotes a channel to OUTPUT
 * on demand, persists the direction mask in NVS and emits a GPIO_STATE event
 * immediately (so the Web UI reflects the real level, not an assumed one).
 *
 * A polling task samples every channel and emits GPIO_STATE only on change,
 * which keeps external signal edges visible in the UI without spamming the bus.
 */
namespace RHD {

class GpioMonitor {
public:
    bool begin();

    // Drive a monitored pin. Returns false for unknown pins / claim failures.
    bool setPin(uint8_t pin, uint8_t value);

    // Switch direction without changing the level. Returns false if unknown.
    bool setDirection(uint8_t pin, bool output);

    // Snapshot of all channels. Arrays must hold GPIO_MONITOR_COUNT entries.
    void states(uint8_t stateOut[], uint8_t dirOut[]) const;

    int      indexOf(uint8_t pin) const;
    uint8_t  level(uint8_t index) const;
    bool     isOutput(uint8_t index) const;
    uint8_t  outputMask() const;

    bool isReady() const { return _ready; }

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();
    void        publish(uint8_t index, uint8_t state);

    bool     _ready = false;
    bool     _isOutput[DebugPins::GPIO_MONITOR_COUNT];
    uint8_t  _last[DebugPins::GPIO_MONITOR_COUNT];
    uint32_t _lastPoll = 0;
    TaskHandle_t _task = nullptr;
};

} // namespace RHD

#endif // DEBUG_GPIO_MONITOR_H_
