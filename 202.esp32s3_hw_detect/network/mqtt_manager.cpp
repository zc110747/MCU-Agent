#include "mqtt_manager.h"
#include "storage/config_manager.h"
#include "storage/log_manager.h"
#include "app/debug_gateway.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

namespace RHD {

MqttManager g_mqtt;
MqttManager* MqttManager::_self = nullptr;

static void mqttCb(char* topic, uint8_t* payload, unsigned int len) {
    if (MqttManager::_self) MqttManager::_self->onMessage(topic, payload, len);
}

bool MqttManager::begin() {
    _self = this;
    _base = String("remote-debugger/") + g_config.deviceId() + "/";
    _pubQueue = xQueueCreate(48, sizeof(DebugEvent));

    auto* wc = new WiFiClient();
    auto* pc = new PubSubClient(*wc);
    pc->setBufferSize(MqttConfig::MAX_PAYLOAD_SIZE + 64);
    pc->setCallback(mqttCb);
    _wifiClient = wc;
    _client = pc;

    LOG_INFO("MQTT", "broker %s:%d", g_config.mqttBroker().c_str(), g_config.mqttPort());

    xTaskCreatePinnedToCore(&MqttManager::taskTrampoline, "mqtt_task",
                            AppConfig::MQTT_TASK_STACK, this,
                            AppConfig::MQTT_TASK_PRIO, &_task,
                            AppConfig::MQTT_TASK_CORE);
    return true;
}

void MqttManager::enqueue(const DebugEvent& ev) {
    if (_pubQueue && xQueueSend(_pubQueue, &ev, 0) != pdTRUE) ++_dropped;
}

void MqttManager::reconnect() {
    auto* pc = static_cast<PubSubClient*>(_client);
    pc->disconnect();
    _connected = false;
}

bool MqttManager::doConnect() {
    auto* pc = static_cast<PubSubClient*>(_client);
    pc->setServer(g_config.mqttBroker().c_str(), (uint16_t)g_config.mqttPort());

    String id = g_config.deviceId();
    String user = g_config.mqttUser();
    String pass = g_config.mqttPassword();
    bool ok;
    if (user.length() > 0) {
        ok = pc->connect(id.c_str(), user.c_str(), pass.c_str(),
                         (_base + "status").c_str(), 1, true, "offline");
    } else {
        ok = pc->connect(id.c_str(),
                         (_base + "status").c_str(), 1, true, "offline");
    }
    if (ok) {
        pc->subscribe((_base + MqttConfig::TOPIC_CMD).c_str());
        _connected = true;
        LOG_INFO("MQTT", "connected as %s", id.c_str());
        if (g_config.mqttTls()) {
            LOG_WARN("MQTT", "TLS requested but v1 uses plain TCP (see limitations)");
        }
    } else {
        _connected = false;
        LOG_WARN("MQTT", "connect failed (state=%d)", pc->state());
    }
    return ok;
}

void MqttManager::taskTrampoline(void* arg) {
    static_cast<MqttManager*>(arg)->taskLoop();
}

void MqttManager::taskLoop() {
    auto* pc = static_cast<PubSubClient*>(_client);
    DebugEvent ev;
    _nextRetry = 0;

    while (true) {
        if (!_connected) {
            uint32_t now = millis();
            if (now >= _nextRetry) {
                if (doConnect()) {
                    _nextRetry = 0;
                } else {
                    _nextRetry = now + MqttConfig::CONNECT_RETRY_MS;
                }
            }
        } else {
            if (!pc->connected()) {
                _connected = false;
                _nextRetry = millis() + MqttConfig::CONNECT_RETRY_MS;
            }
        }

        if (_connected) {
            pc->loop();
            while (_pubQueue && xQueueReceive(_pubQueue, &ev, 0) == pdTRUE) {
                publishEvent(ev);
            }
        } else {
            // drop queued events while disconnected to avoid backlog growth
            while (_pubQueue && xQueueReceive(_pubQueue, &ev, 0) == pdTRUE) {
                ++_dropped;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void MqttManager::publishEvent(const DebugEvent& ev) {
    auto* pc = static_cast<PubSubClient*>(_client);
    JsonDocument d;
    char txt[AppConfig::EVENT_DATA_MAX + 1];
    String topic;

    switch (ev.type) {
        case DebugEventType::UART_RX:
        case DebugEventType::UART_TX: {
            size_t n = ev.length < AppConfig::EVENT_DATA_MAX ? ev.length : AppConfig::EVENT_DATA_MAX;
            memcpy(txt, ev.data, n);
            txt[n] = '\0';
            d["timestamp"] = ev.timestamp;
            d["channel"] = ev.channel;
            d["encoding"] = (ev.encoding == 1) ? "hex" : "text";
            d["data"] = txt;
            topic = _base + ((ev.type == DebugEventType::UART_RX) ? MqttConfig::TOPIC_UART_RX
                                                                  : MqttConfig::TOPIC_UART_TX);
            break;
        }
        case DebugEventType::ADC_SAMPLE:
            d["timestamp"] = ev.timestamp;
            d["channel"] = ev.channel;
            d["raw"] = ev.raw;
            d["voltage"] = ev.voltage;
            topic = _base + MqttConfig::TOPIC_ADC_CH + String(ev.channel);
            break;
        case DebugEventType::GPIO_STATE:
            d["timestamp"] = ev.timestamp;
            d["gpio"] = ev.gpio;
            d["state"] = ev.state;
            topic = _base + MqttConfig::TOPIC_GPIO;
            break;
        case DebugEventType::SYSTEM:
            pc->publish((_base + MqttConfig::TOPIC_SYSTEM).c_str(), (const char*)ev.data);
            return;
        case DebugEventType::ERROR:
            pc->publish((_base + MqttConfig::TOPIC_EVENT).c_str(), (const char*)ev.data);
            return;
        default:
            return;
    }
    String s;
    serializeJson(d, s);
    if (!pc->publish(topic.c_str(), s.c_str())) {
        ++_dropped;
    }
}

void MqttManager::onMessage(char* topic, uint8_t* payload, unsigned int len) {
    JsonDocument d;
    if (deserializeJson(d, payload, len) != DeserializationError::Code::Ok) {
        LOG_WARN("MQTT", "bad command json");
        return;
    }
    LOG_INFO("MQTT", "cmd on %s", topic);
    getGateway().handleJsonCommand(d.as<JsonObjectConst>());
}

} // namespace RHD
