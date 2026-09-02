#include "websocket_manager.h"
#include "storage/log_manager.h"
#include "app/debug_gateway.h"
#include "config/pin_config.h"
#include "bsp/gpio_monitor.h"
#include "bsp/ws2812_led.h"
#include "bsp/pwm_output.h"
#include <ArduinoJson.h>
#include <WebSocketsServer.h>

namespace RHD {

WebSocketManager g_ws;
WebSocketManager* WebSocketManager::_self = nullptr;

static void wsCb(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
    if (!WebSocketManager::_self) return;
    switch (type) {
        case WStype_CONNECTED:
            LOG_INFO("WS", "client %u connected", num);
            WebSocketManager::_self->onConnect();
            break;
        case WStype_DISCONNECTED:
            LOG_INFO("WS", "client %u disconnected", num);
            WebSocketManager::_self->onDisconnect();
            break;
        case WStype_TEXT:
            WebSocketManager::_self->onText(num, payload, len);
            break;
        default:
            break;
    }
}

void WebSocketManager::onConnect() {
    if (_clients < 255) ++_clients;
    // Push a snapshot immediately so a freshly loaded page is never blank.
    _lastState = 0;
}

void WebSocketManager::onDisconnect() {
    if (_clients > 0) --_clients;
}

bool WebSocketManager::begin() {
    _self = this;
    _evQueue = xQueueCreate(EV_QUEUE_LEN, sizeof(DebugEvent));
    _logQueue = xQueueCreate(LOG_QUEUE_LEN, sizeof(WsLogLine));

    auto* srv = new WebSocketsServer(AppConfig::WEBSOCKET_PORT);
    srv->begin();
    srv->onEvent(wsCb);
    _server = srv;
    _ready = true;
    LOG_INFO("WS", "server on port %u", AppConfig::WEBSOCKET_PORT);

    xTaskCreatePinnedToCore(&WebSocketManager::taskTrampoline, "ws_task",
                            AppConfig::WEB_TASK_STACK, this,
                            AppConfig::WEB_TASK_PRIO, &_task,
                            AppConfig::WEB_TASK_CORE);
    return true;
}

void WebSocketManager::enqueue(const DebugEvent& ev) {
    if (_evQueue && xQueueSend(_evQueue, &ev, 0) != pdTRUE) ++_dropped;
}

void WebSocketManager::enqueueLog(const char* line) {
    if (!_logQueue) return;
    WsLogLine l;
    strncpy(l.text, line, sizeof(l.text) - 1);
    l.text[sizeof(l.text) - 1] = '\0';
    if (xQueueSend(_logQueue, &l, 0) != pdTRUE) { /* drop */ }
}

void WebSocketManager::taskTrampoline(void* arg) {
    static_cast<WebSocketManager*>(arg)->taskLoop();
}

void WebSocketManager::taskLoop() {
    auto* srv = static_cast<WebSocketsServer*>(_server);
    DebugEvent ev;
    WsLogLine logline;
    while (true) {
        srv->loop();

        while (_evQueue && xQueueReceive(_evQueue, &ev, 0) == pdTRUE) {
            broadcastEvent(ev);
        }
        while (_logQueue && xQueueReceive(_logQueue, &logline, 0) == pdTRUE) {
            JsonDocument d;
            d["type"] = "log";
            d["line"] = logline.text;
            String s;
            serializeJson(d, s);
            srv->broadcastTXT(s);
        }

        // Interface state snapshot (GPIO / LED / PWM / ADC). Only built when a
        // browser is attached, so an idle device pays nothing for it.
        if (_clients > 0 && (millis() - _lastState >= AppConfig::STATE_PUSH_INTERVAL_MS)) {
            _lastState = millis();
            broadcastState();
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void WebSocketManager::broadcastState() {
    auto* srv = static_cast<WebSocketsServer*>(_server);
    DebugGateway& gw = getGateway();

    JsonDocument d;
    d["type"] = "state";
    d["ts"]   = millis();

    // ---- GPIO ----
    {
        uint8_t st[DebugPins::GPIO_MONITOR_COUNT];
        uint8_t dir[DebugPins::GPIO_MONITOR_COUNT];
        GpioMonitor* gpio = gw.gpio();
        if (gpio) {
            gpio->states(st, dir);
            JsonArray arr = d.createNestedArray("gpio");
            for (int i = 0; i < DebugPins::GPIO_MONITOR_COUNT; ++i) {
                JsonObject o = arr.createNestedObject();
                o["pin"]   = DebugPins::GPIO_MONITOR_PINS[i];
                o["state"] = st[i] ? 1 : 0;
                o["dir"]   = dir[i] ? 1 : 0;
            }
        }
    }

    // ---- WS2812 (mode + live output) ----
    if (gw.led()) {
        JsonObject led = d.createNestedObject("led");
        led["pin"]    = DebugPins::RUN_LED;
        led["mode"]   = (int)gw.led()->mode();
        led["mode_str"] = gw.led()->modeStr();
        led["on"]     = gw.led()->isOn() ? 1 : 0;
        led["r"]      = gw.led()->red();
        led["g"]      = gw.led()->green();
        led["b"]      = gw.led()->blue();
        led["ready"]  = gw.led()->isReady() ? 1 : 0;
    }

    // ---- PWM ----
    if (gw.pwm()) {
        JsonObject pwm = d.createNestedObject("pwm");
        pwm["active"]     = gw.pwm()->isActive() ? 1 : 0;
        pwm["pin"]        = gw.pwm()->pin();
        pwm["period"]     = gw.pwm()->periodUs();
        pwm["duty"]       = gw.pwm()->dutyPct();
        pwm["freq"]       = gw.pwm()->freqHz();
        pwm["resolution"] = gw.pwm()->resolution();
    }

    // ---- ADC ----
    if (gw.adc()) {
        uint16_t raw[4];
        float    volts[4];
        gw.adc()->latest(raw, volts);
        JsonArray arr = d.createNestedArray("adc");
        for (int i = 0; i < AppConfig::ADC_CHANNEL_COUNT; ++i) {
            JsonObject o = arr.createNestedObject();
            o["ch"]      = i;
            o["raw"]     = raw[i];
            o["voltage"] = volts[i];
        }
        d["adc_src"]    = gw.adc()->adcSource();
        d["adc_ready"]  = gw.adc()->isReady() ? 1 : 0;
    }

    String s;
    serializeJson(d, s);
    srv->broadcastTXT(s);
}

void WebSocketManager::broadcastEvent(const DebugEvent& ev) {
    auto* srv = static_cast<WebSocketsServer*>(_server);
    JsonDocument d;
    char txt[AppConfig::EVENT_DATA_MAX + 1];

    switch (ev.type) {
        case DebugEventType::UART_RX:
        case DebugEventType::UART_TX: {
            size_t n = ev.length < AppConfig::EVENT_DATA_MAX ? ev.length : AppConfig::EVENT_DATA_MAX;
            memcpy(txt, ev.data, n);
            txt[n] = '\0';
            d["type"] = (ev.type == DebugEventType::UART_RX) ? "uart" : "uart_tx";
            d["timestamp"] = ev.timestamp;
            d["encoding"] = (ev.encoding == 1) ? "hex" : "text";
            d["data"] = txt;
            break;
        }
        case DebugEventType::ADC_SAMPLE:
            d["type"] = "adc";
            d["channel"] = ev.channel;
            d["raw"] = ev.raw;
            d["voltage"] = ev.voltage;
            d["timestamp"] = ev.timestamp;
            break;
        case DebugEventType::GPIO_STATE:
            d["type"] = "gpio";
            d["gpio"] = ev.gpio;
            d["state"] = ev.state;
            d["timestamp"] = ev.timestamp;
            break;
        case DebugEventType::LED_STATE:
        case DebugEventType::PWM_STATE:
            // Full state already serialized as JSON in ev.data
            // (LED also carries the live on/off + RGB triplet).
            srv->broadcastTXT((const char*)ev.data);
            return;
        case DebugEventType::SYSTEM:
        case DebugEventType::ERROR:
            // data already contains a serialized JSON object
            srv->broadcastTXT((const char*)ev.data);
            return;
        default:
            return;
    }
    String s;
    serializeJson(d, s);
    srv->broadcastTXT(s);
}

void WebSocketManager::onText(uint8_t num, uint8_t* payload, size_t len) {
    JsonDocument d;
    DeserializationError err = deserializeJson(d, payload, len);
    if (err) {
        LOG_WARN("WS", "bad json from client %u", num);
        return;
    }
    if (d["cmd"].is<const char*>()) {
        getGateway().handleJsonCommand(d.as<JsonObjectConst>());
    }
}

} // namespace RHD
