#include "websocket_manager.h"
#include "storage/log_manager.h"
#include "app/debug_gateway.h"
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
            break;
        case WStype_DISCONNECTED:
            LOG_INFO("WS", "client %u disconnected", num);
            break;
        case WStype_TEXT:
            WebSocketManager::_self->onText(num, payload, len);
            break;
        default:
            break;
    }
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
        vTaskDelay(pdMS_TO_TICKS(5));
    }
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
            d["type"] = "led";
            d["gpio"] = ev.gpio;
            d["mode"] = (int)ev.state;   // 0=off,1=r,2=g,3=b,4=cycle
            d["timestamp"] = ev.timestamp;
            break;
        case DebugEventType::PWM_STATE:
            // Full state already serialized as JSON in ev.data.
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
