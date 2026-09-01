#ifndef NETWORK_WEB_SERVER_H_
#define NETWORK_WEB_SERVER_H_

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config/app_config.h"

/**
 * @file web_server.h
 * @brief Built-in HTTP server: dashboard, REST API and Web OTA.
 *
 * Serves the single-page dashboard at "/" plus JSON REST endpoints for status,
 * UART/ADC/GPIO config and control, WiFi/MQTT setup, logs, reset and firmware
 * upload. Mutating endpoints require authentication (Basic Auth or ?pw=).
 */
namespace RHD {

class WebServerManager {
public:
    bool begin();
    bool isReady() const { return _ready; }

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();

    bool        authenticated();
    void        sendJson(JsonDocument& d, int code = 200);
    void        handleRoot();
    void        handleStatus();
    void        handleUartConfig();
    void        handleAdcConfig();
    void        handleAdcRead();
    void        handleGpio();
    void        handleWifi();
    void        handleMqtt();
    void        handleWebPw();
    void        handleCommand();
    void        handleReset();
    void        handleLogs();
    void        handleUpdateUpload();
    void        handleUpdateFinish();

    bool _ready = false;
    void* _server = nullptr; // WebServer*
    TaskHandle_t _task = nullptr;
    bool _otaAuthOk = false;
};

} // namespace RHD

#endif // NETWORK_WEB_SERVER_H_
