#include "bsp/gpio_monitor.h"
#include "app/event_bus.h"
#include "app/pin_manager.h"
#include "storage/config_manager.h"
#include "storage/log_manager.h"

namespace RHD {

bool GpioMonitor::begin() {
    uint8_t mask = g_config.gpioOutputMask();

    for (int i = 0; i < DebugPins::GPIO_MONITOR_COUNT; ++i) {
        int pin = DebugPins::GPIO_MONITOR_PINS[i];
        if (!DebugPins::g_pinManager.claim(pin, "GPIO", false)) {
            LOG_ERROR("GPIO", "cannot claim GPIO%d", pin);
            return false;
        }
        _isOutput[i] = (mask & (1u << i)) != 0;
        if (_isOutput[i]) {
            pinMode(pin, OUTPUT);
            digitalWrite(pin, LOW);
        } else {
            pinMode(pin, INPUT_PULLUP);
        }
        _last[i] = (uint8_t)(digitalRead(pin) ? 1 : 0);
    }

    _ready = true;
    LOG_INFO("GPIO", "monitor ready on GPIO%d/%d/%d/%d (output mask 0x%02X)",
             DebugPins::GPIO_MONITOR_PINS[0], DebugPins::GPIO_MONITOR_PINS[1],
             DebugPins::GPIO_MONITOR_PINS[2], DebugPins::GPIO_MONITOR_PINS[3],
             (unsigned)mask);

    xTaskCreatePinnedToCore(&GpioMonitor::taskTrampoline, "gpio_task",
                            AppConfig::GPIO_TASK_STACK, this,
                            AppConfig::GPIO_TASK_PRIO, &_task,
                            AppConfig::GPIO_TASK_CORE);
    return true;
}

int GpioMonitor::indexOf(uint8_t pin) const {
    for (int i = 0; i < DebugPins::GPIO_MONITOR_COUNT; ++i) {
        if (DebugPins::GPIO_MONITOR_PINS[i] == (int)pin) return i;
    }
    return -1;
}

uint8_t GpioMonitor::level(uint8_t index) const {
    if (index >= (uint8_t)DebugPins::GPIO_MONITOR_COUNT) return 0;
    return _last[index];
}

bool GpioMonitor::isOutput(uint8_t index) const {
    if (index >= (uint8_t)DebugPins::GPIO_MONITOR_COUNT) return false;
    return _isOutput[index];
}

uint8_t GpioMonitor::outputMask() const {
    uint8_t mask = 0;
    for (int i = 0; i < DebugPins::GPIO_MONITOR_COUNT; ++i) {
        if (_isOutput[i]) mask |= (uint8_t)(1u << i);
    }
    return mask;
}

void GpioMonitor::states(uint8_t stateOut[], uint8_t dirOut[]) const {
    for (int i = 0; i < DebugPins::GPIO_MONITOR_COUNT; ++i) {
        stateOut[i] = _last[i];
        dirOut[i]   = _isOutput[i] ? 1 : 0;
    }
}

void GpioMonitor::publish(uint8_t index, uint8_t state) {
    if (index >= (uint8_t)DebugPins::GPIO_MONITOR_COUNT) return;
    DebugEvent ev;
    ev.type      = DebugEventType::GPIO_STATE;
    ev.timestamp = millis();
    ev.gpio      = (uint8_t)DebugPins::GPIO_MONITOR_PINS[index];
    ev.state     = state ? 1 : 0;
    g_eventBus.push(ev);
}

bool GpioMonitor::setDirection(uint8_t pin, bool output) {
    if (!_ready) return false;
    int idx = indexOf(pin);
    if (idx < 0) {
        LOG_WARN("GPIO", "setDirection rejected: GPIO%d not monitored", pin);
        return false;
    }
    if (_isOutput[idx] == output) return true;

    _isOutput[idx] = output;
    pinMode((uint8_t)DebugPins::GPIO_MONITOR_PINS[idx], output ? OUTPUT : INPUT_PULLUP);
    if (output) digitalWrite((uint8_t)DebugPins::GPIO_MONITOR_PINS[idx], LOW);

    g_config.setGpioOutputMask(outputMask());
    g_config.save();

    _last[idx] = (uint8_t)(digitalRead((uint8_t)DebugPins::GPIO_MONITOR_PINS[idx]) ? 1 : 0);
    publish((uint8_t)idx, _last[idx]);
    return true;
}

bool GpioMonitor::setPin(uint8_t pin, uint8_t value) {
    if (!_ready) return false;
    int idx = indexOf(pin);
    if (idx < 0) {
        LOG_WARN("GPIO", "setPin rejected: GPIO%d not a monitored pin", pin);
        return false;
    }

    // Promote to output on first write (persisted so it survives a reboot).
    if (!_isOutput[idx]) {
        _isOutput[idx] = true;
        pinMode((uint8_t)DebugPins::GPIO_MONITOR_PINS[idx], OUTPUT);
        g_config.setGpioOutputMask(outputMask());
        g_config.save();
    }

    digitalWrite((uint8_t)DebugPins::GPIO_MONITOR_PINS[idx], value ? HIGH : LOW);

    // Read back so the UI always shows the real electrical level.
    uint8_t actual = (uint8_t)(digitalRead((uint8_t)DebugPins::GPIO_MONITOR_PINS[idx]) ? 1 : 0);
    _last[idx] = actual;
    publish((uint8_t)idx, actual);
    return true;
}

void GpioMonitor::taskTrampoline(void* arg) {
    static_cast<GpioMonitor*>(arg)->taskLoop();
}

void GpioMonitor::taskLoop() {
    while (true) {
        for (int i = 0; i < DebugPins::GPIO_MONITOR_COUNT; ++i) {
            uint8_t v = (uint8_t)(digitalRead((uint8_t)DebugPins::GPIO_MONITOR_PINS[i]) ? 1 : 0);
            if (v != _last[i]) {
                _last[i] = v;
                publish((uint8_t)i, v);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

} // namespace RHD
