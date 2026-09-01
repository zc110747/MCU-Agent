#ifndef APP_DEBUG_GATEWAY_H_
#define APP_DEBUG_GATEWAY_H_

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "app/debug_module.h"

// Forward declarations (managers are owned by the gateway, defined in .cpp).
namespace RHD {
class UartMonitor;
class AdcMonitor;
class GpioMonitor;
class WiFiManager;
class MqttManager;
class WebSocketManager;
class WebServerManager;
class OtaManager;
class ConfigManager;

/**
 * @file debug_gateway.h
 * @brief Top-level orchestrator: owns all modules, runs the startup sequence,
 *        the EventTask (bus -> WS/MQTT fan-out), and the command dispatcher.
 */
class DebugGateway {
public:
    bool begin();

    // Central command entry used by MQTT and WebSocket inbound messages.
    void handleJsonCommand(const JsonObjectConst& req);

    // Module accessors (used by Web/MQTT/WS layers).
    UartMonitor*      uart()  { return _uart; }
    AdcMonitor*       adc()   { return _adc; }
    GpioMonitor*      gpio()  { return _gpio; }
    WiFiManager*      wifi()  { return _wifi; }
    MqttManager*      mqtt()  { return _mqtt; }
    WebSocketManager* ws()    { return _ws; }
    WebServerManager* web()   { return _web; }
    OtaManager*       ota()   { return _ota; }
    ConfigManager*    config(){ return _config; }

private:
    void startEventTask();
    void eventTaskLoop();
    void publishSystemStatus();
    void publishError(const char* msg);

    static void eventTaskTrampoline(void* arg);

    UartMonitor*      _uart   = nullptr;
    AdcMonitor*       _adc    = nullptr;
    GpioMonitor*      _gpio   = nullptr;
    WiFiManager*      _wifi   = nullptr;
    MqttManager*      _mqtt   = nullptr;
    WebSocketManager* _ws     = nullptr;
    WebServerManager* _web    = nullptr;
    OtaManager*       _ota    = nullptr;
    ConfigManager*    _config = nullptr;

    TaskHandle_t _eventTask = nullptr;
    uint32_t     _lastStatus = 0;
};

// Single global accessor (avoids init-order issues vs. a raw global object).
DebugGateway& getGateway();

} // namespace RHD

#endif // APP_DEBUG_GATEWAY_H_
