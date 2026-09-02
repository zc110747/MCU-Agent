#include "bsp/ws2812_led.h"
#include "app/event_bus.h"
#include "app/pin_manager.h"
#include "storage/log_manager.h"
#include <Adafruit_NeoPixel.h>

namespace RHD {

const char* Ws2812Controller::modeName(Ws2812Mode m) {
    switch (m) {
        case Ws2812Mode::OFF:       return "off";
        case Ws2812Mode::BLINK_R:   return "r";
        case Ws2812Mode::BLINK_G:   return "g";
        case Ws2812Mode::BLINK_B:   return "b";
        case Ws2812Mode::CYCLE_RGB: return "cycle";
        default:                    return "off";
    }
}

Ws2812Mode Ws2812Controller::modeFromName(const char* name) {
    if (!name) return Ws2812Mode::OFF;
    if (strcmp(name, "r") == 0)      return Ws2812Mode::BLINK_R;
    if (strcmp(name, "g") == 0)      return Ws2812Mode::BLINK_G;
    if (strcmp(name, "b") == 0)      return Ws2812Mode::BLINK_B;
    if (strcmp(name, "cycle") == 0)  return Ws2812Mode::CYCLE_RGB;
    return Ws2812Mode::OFF;
}

bool Ws2812Controller::begin() {
    if (!DebugPins::g_pinManager.claim(DebugPins::RUN_LED, "LED", true)) {
        LOG_ERROR("LED", "cannot claim RUN LED GPIO%d", DebugPins::RUN_LED);
        return false;
    }

    auto* px = new Adafruit_NeoPixel(1, DebugPins::RUN_LED, NEO_GRB + NEO_KHZ800);
    px->begin();
    px->setBrightness(BRIGHTNESS);
    px->clear();
    px->show();
    _pixels = px;
    _ready  = true;

    LOG_INFO("LED", "WS2812B ready on GPIO%d (brightness %u)", DebugPins::RUN_LED, BRIGHTNESS);

    xTaskCreatePinnedToCore(&Ws2812Controller::taskTrampoline, "led_task",
                            AppConfig::LED_TASK_STACK, this,
                            AppConfig::LED_TASK_PRIO, &_task,
                            AppConfig::LED_TASK_CORE);
    return true;
}

void Ws2812Controller::applyOutput() {
    if (!_ready || !_pixels) return;
    auto* px = static_cast<Adafruit_NeoPixel*>(_pixels);
    if (_on) {
        px->setPixelColor(0, px->Color(_r, _g, _b));
    } else {
        px->setPixelColor(0, (uint32_t)0);
    }
    px->show();
}

void Ws2812Controller::setMode(Ws2812Mode m) {
    if (_mode == m) return;
    _mode = m;
    _cycleStep = 0;
    _lastToggle = 0;          // force an immediate refresh on the next tick
    publishState();
    LOG_INFO("LED", "mode -> %s", modeStr());
}

void Ws2812Controller::toJson(String& out) const {
    out += "{\"type\":\"led\",\"gpio\":";
    out += String(DebugPins::RUN_LED);
    out += ",\"mode\":";
    out += String((int)_mode);
    out += ",\"mode_str\":\"";
    out += modeStr();
    out += "\",\"on\":";
    out += _on ? "1" : "0";
    out += ",\"r\":";
    out += String(_r);
    out += ",\"g\":";
    out += String(_g);
    out += ",\"b\":";
    out += String(_b);
    out += ",\"brightness\":";
    out += String((int)BRIGHTNESS);
    out += ",\"timestamp\":";
    out += String(millis());
    out += "}";
}

void Ws2812Controller::publishState() {
    String payload;
    toJson(payload);

    DebugEvent ev;
    ev.type      = DebugEventType::LED_STATE;
    ev.timestamp = millis();
    ev.gpio      = (uint8_t)DebugPins::RUN_LED;
    ev.state     = (uint8_t)_mode;
    size_t n = payload.length();
    if (n > AppConfig::EVENT_DATA_MAX - 1) n = AppConfig::EVENT_DATA_MAX - 1;
    memcpy(ev.data, payload.c_str(), n);
    ev.data[n] = '\0';
    ev.length = (uint16_t)n;
    g_eventBus.push(ev);
}

void Ws2812Controller::taskTrampoline(void* arg) {
    static_cast<Ws2812Controller*>(arg)->taskLoop();
}

void Ws2812Controller::taskLoop() {
    while (true) {
        if (_ready) {
            uint32_t now = millis();
            if (now - _lastToggle >= BLINK_HALF_PERIOD_MS) {
                _lastToggle = now;
                bool phase = ((now / BLINK_HALF_PERIOD_MS) & 1) != 0;

                switch (_mode) {
                    case Ws2812Mode::OFF:
                        _on = false; _r = _g = _b = 0;
                        break;
                    case Ws2812Mode::BLINK_R:
                        _on = phase; _r = 255; _g = 0; _b = 0;
                        break;
                    case Ws2812Mode::BLINK_G:
                        _on = phase; _r = 0; _g = 255; _b = 0;
                        break;
                    case Ws2812Mode::BLINK_B:
                        _on = phase; _r = 0; _g = 0; _b = 255;
                        break;
                    case Ws2812Mode::CYCLE_RGB:
                        _on = true;
                        _cycleStep = (uint8_t)((_cycleStep + 1) % 3);
                        _r = (_cycleStep == 0) ? 255 : 0;
                        _g = (_cycleStep == 1) ? 255 : 0;
                        _b = (_cycleStep == 2) ? 255 : 0;
                        break;
                    default:
                        _on = false;
                        break;
                }
                applyOutput();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

} // namespace RHD
