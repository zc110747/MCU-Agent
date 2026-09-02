#ifndef DEBUG_PWM_OUTPUT_H_
#define DEBUG_PWM_OUTPUT_H_

#pragma once
#include <Arduino.h>
#include "app/debug_module.h"

/**
 * @file pwm_output.h
 * @brief Single-channel PWM generator on the ESP32 LEDC peripheral.
 *
 * A free GPIO is picked at runtime from the Web UI (see the Interface page).
 * The pin is claimed through PinManager, so it can never collide with UART /
 * I2C / the monitor pins / the RUN LED, and is released again on stop().
 *
 * Every successful change emits a PWM_STATE event carrying the full JSON
 * payload, so the browser shows the *actual* applied parameters (rounded by the
 * LEDC clock divider) instead of echoing what was typed.
 */
namespace RHD {

class PwmController {
public:
    static const uint32_t PERIOD_MIN_US = 25;    // 40 kHz
    static const uint32_t PERIOD_MAX_US = 1000000; // 1 Hz
    static const uint8_t  RES_MAX = 12;
    static const uint8_t  RES_MIN = 8;

    bool begin();

    // Start / reconfigure. `periodUs` is the full period, `dutyPct` 0..100.
    bool configure(uint8_t pin, uint32_t periodUs, uint8_t dutyPct);
    bool setDuty(uint8_t dutyPct);
    void stop();

    bool     isActive() const  { return _active; }
    int      pin() const       { return _active ? (int)_pin : -1; }
    uint32_t periodUs() const  { return _periodUs; }
    uint8_t  dutyPct() const   { return _dutyPct; }
    uint32_t freqHz() const    { return _freqHz; }
    uint8_t  resolution() const { return _resolution; }
    bool     isReady() const   { return _ready; }

    // Serialize the applied PWM state as {"type":"pwm",...} into `out`.
    void toJson(String& out) const;

private:
    void publishState();
    static uint8_t pickResolution(uint32_t freqHz);

    bool     _ready      = false;
    bool     _active     = false;
    uint8_t  _pin        = 0;
    uint32_t _periodUs   = 0;
    uint32_t _freqHz     = 0;
    uint8_t  _dutyPct    = 0;
    uint8_t  _resolution = 10;
};

} // namespace RHD

#endif // DEBUG_PWM_OUTPUT_H_
