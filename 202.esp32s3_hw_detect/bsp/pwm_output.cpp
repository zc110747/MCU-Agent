#include "bsp/pwm_output.h"
#include "app/event_bus.h"
#include "app/pin_manager.h"
#include "storage/log_manager.h"
#include <esp32-hal-ledc.h>

namespace RHD {

bool PwmController::begin() {
    _ready = true;   // no fixed pin: the pin is chosen at configure() time
    LOG_INFO("PWM", "LEDC ready (pin selected at runtime, %u..%u us period)",
             (unsigned)PERIOD_MIN_US, (unsigned)PERIOD_MAX_US);
    return true;
}

uint8_t PwmController::pickResolution(uint32_t freqHz) {
    // LEDC needs freq * 2^resolution <= clock (APB 80 MHz).
    const double clk = 80000000.0;
    uint8_t res = RES_MAX;
    while (res > RES_MIN && (clk / (double)(1UL << res)) < (double)freqHz) {
        --res;
    }
    return res;
}

bool PwmController::configure(uint8_t pin, uint32_t periodUs, uint8_t dutyPct) {
    if (!_ready) return false;

    if (periodUs < PERIOD_MIN_US)  periodUs = PERIOD_MIN_US;
    if (periodUs > PERIOD_MAX_US)  periodUs = PERIOD_MAX_US;
    if (dutyPct > 100)             dutyPct = 100;

    uint32_t freq = 0;
    if (periodUs > 0) freq = 1000000UL / periodUs;
    if (freq == 0) freq = 1;

    // Re-using the same pin: just update frequency/duty in place.
    if (_active && _pin == pin) {
        if (freq != _freqHz) {
            uint8_t res = pickResolution(freq);
            if (!ledcChangeFrequency(pin, freq, res)) {
                LOG_WARN("PWM", "ledcChangeFrequency(%u Hz, %u bit) failed", (unsigned)freq, (unsigned)res);
                return false;
            }
            _freqHz     = freq;
            _resolution = res;
        }
        _periodUs = periodUs;
        _dutyPct  = dutyPct;
        uint32_t maxDuty = (1UL << _resolution) - 1;
        ledcWrite(pin, (uint32_t)((uint64_t)maxDuty * dutyPct / 100));
        publishState();
        return true;
    }

    // Switching pins: release the old one first.
    if (_active) stop();

    if (DebugPins::g_pinManager.isReserved(pin)) {
        LOG_WARN("PWM", "rejected: GPIO%d is reserved", pin);
        return false;
    }
    if (DebugPins::g_pinManager.isClaimed(pin)) {
        LOG_WARN("PWM", "rejected: GPIO%d already claimed", pin);
        return false;
    }

    uint8_t res = pickResolution(freq);
    if (!ledcAttach(pin, freq, res)) {
        LOG_WARN("PWM", "ledcAttach(GPIO%d, %u Hz, %u bit) failed", pin, (unsigned)freq, (unsigned)res);
        return false;
    }
    if (!DebugPins::g_pinManager.claim((int)pin, "PWM", true)) {
        ledcDetach(pin);
        LOG_WARN("PWM", "rejected: claim GPIO%d failed", pin);
        return false;
    }

    _active     = true;
    _pin        = pin;
    _periodUs   = periodUs;
    _freqHz     = freq;
    _dutyPct    = dutyPct;
    _resolution = res;

    uint32_t maxDuty = (1UL << res) - 1;
    ledcWrite(pin, (uint32_t)((uint64_t)maxDuty * dutyPct / 100));

    LOG_INFO("PWM", "GPIO%d %u us (%u Hz) duty %u%% res %u-bit",
             pin, (unsigned)periodUs, (unsigned)freq, (unsigned)dutyPct, (unsigned)res);
    publishState();
    return true;
}

bool PwmController::setDuty(uint8_t dutyPct) {
    if (!_active) return false;
    if (dutyPct > 100) dutyPct = 100;
    _dutyPct = dutyPct;
    uint32_t maxDuty = (1UL << _resolution) - 1;
    ledcWrite(_pin, (uint32_t)((uint64_t)maxDuty * dutyPct / 100));
    publishState();
    return true;
}

void PwmController::stop() {
    if (!_active) return;
    // Drive the line low before detaching so the pin does not float high.
    ledcWrite(_pin, 0);
    ledcDetach(_pin);
    DebugPins::g_pinManager.release((int)_pin);
    digitalWrite(_pin, LOW);
    pinMode(_pin, INPUT);
    LOG_INFO("PWM", "stopped, GPIO%d released", (unsigned)_pin);

    _active   = false;
    _pin      = 0;
    _periodUs = 0;
    _freqHz   = 0;
    _dutyPct  = 0;
    publishState();
}

void PwmController::toJson(String& out) const {
    out += "{\"type\":\"pwm\",\"active\":";
    out += _active ? "1" : "0";
    out += ",\"pin\":";
    out += String((int)_pin);
    out += ",\"period\":";
    out += String(_periodUs);
    out += ",\"duty\":";
    out += String((int)_dutyPct);
    out += ",\"freq\":";
    out += String(_freqHz);
    out += ",\"resolution\":";
    out += String((int)_resolution);
    out += ",\"timestamp\":";
    out += String(millis());
    out += "}";
}

void PwmController::publishState() {
    String payload;
    toJson(payload);

    DebugEvent ev;
    ev.type      = DebugEventType::PWM_STATE;
    ev.timestamp = millis();
    ev.gpio      = _active ? _pin : 0;
    ev.state     = _active ? 1 : 0;
    size_t n = payload.length();
    if (n > AppConfig::EVENT_DATA_MAX - 1) n = AppConfig::EVENT_DATA_MAX - 1;
    memcpy(ev.data, payload.c_str(), n);
    ev.data[n] = '\0';
    ev.length = (uint16_t)n;
    g_eventBus.push(ev);
}

} // namespace RHD
