#ifndef DEBUG_WS2812_LED_H_
#define DEBUG_WS2812_LED_H_

#pragma once
#include <Arduino.h>
#include "app/debug_module.h"
#include "config/pin_config.h"

/**
 * @file ws2812_led.h
 * @brief On-board WS2812B status LED (GPIO48) driven by Adafruit_NeoPixel (RMT).
 *
 * The controller owns GPIO48 exclusively (claimed via PinManager). Five modes
 * are supported; a FreeRTOS task runs the blink/cycle state machine so the
 * main loop stays free.
 *
 * Besides the mode itself, the controller also reports the *live* output state
 * (currently lit or not, plus the current RGB triplet). That live state is what
 * the web UI shows, so "RGB cycle" is distinguishable from "currently green".
 */
namespace RHD {

enum class Ws2812Mode : uint8_t {
    OFF      = 0,
    BLINK_R  = 1,
    BLINK_G  = 2,
    BLINK_B  = 3,
    CYCLE_RGB = 4
};

class Ws2812Controller {
public:
    // Half period of the blink / cycle state machine.
    static const uint32_t BLINK_HALF_PERIOD_MS = 250;
    static const uint8_t  BRIGHTNESS           = 40;

    bool begin();

    void        setMode(Ws2812Mode m);
    Ws2812Mode  mode() const     { return _mode; }
    const char* modeStr() const  { return modeName(_mode); }

    // Live output state (updated by the LED task).
    bool     isOn() const { return _on; }
    uint8_t  red() const  { return _r; }
    uint8_t  green() const { return _g; }
    uint8_t  blue() const { return _b; }

    bool isReady() const { return _ready; }

    // Serialize the current LED state as {"type":"led",...} into `out`.
    void  toJson(String& out) const;

    // Mode <-> string helpers (strings are the wire format: off/r/g/b/cycle).
    static const char*  modeName(Ws2812Mode m);
    static Ws2812Mode   modeFromName(const char* name);

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();
    void        applyOutput();
    void        publishState();

    void*        _pixels = nullptr;  // Adafruit_NeoPixel*
    Ws2812Mode   _mode   = Ws2812Mode::OFF;
    bool         _ready  = false;
    bool         _on     = false;
    uint8_t      _r = 0, _g = 0, _b = 0;
    uint8_t      _cycleStep = 0;
    uint32_t     _lastToggle = 0;
    TaskHandle_t _task = nullptr;
};

} // namespace RHD

#endif // DEBUG_WS2812_LED_H_
