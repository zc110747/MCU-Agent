#ifndef NETWORK_MQTT_MANAGER_H_
#define NETWORK_MQTT_MANAGER_H_

#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "app/debug_module.h"
#include "config/app_config.h"
#include "config/mqtt_config.h"

/**
 * @file mqtt_manager.h
 * @brief MQTT client (publish/subscribe) with auto-reconnect.
 *
 * Events are queued (enqueue) and published by the MQTT task so a slow/broken
 * broker never blocks the EventTask or UART. Inbound commands on
 * <base>/cmd are forwarded to the gateway command dispatcher.
 */
namespace RHD {

class MqttManager {
public:
    bool begin();
    bool connected() const { return _connected; }

    void enqueue(const DebugEvent& ev);   // non-blocking
    uint32_t droppedEvents() const { return _dropped; }

    // (Re)configure broker from current ConfigManager values and reconnect.
    void reconnect();

    // Used by the static PubSubClient callback (free function).
    static MqttManager* _self;
    void onMessage(char* topic, uint8_t* payload, unsigned int len);

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();
    bool        doConnect();
    void        publishEvent(const DebugEvent& ev);

    bool _connected = false;
    QueueHandle_t _pubQueue = nullptr;
    uint32_t _dropped = 0;
    TaskHandle_t _task = nullptr;
    String _base;       // remote-debugger/<deviceId>/
    uint32_t _nextRetry = 0;

    // Heavy clients kept as opaque pointers (WiFiClient / PubSubClient).
    void* _wifiClient = nullptr;
    void* _client = nullptr;
};

extern MqttManager g_mqtt;

} // namespace RHD

#endif // NETWORK_MQTT_MANAGER_H_
