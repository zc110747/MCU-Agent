#ifndef NETWORK_WEBSOCKET_MANAGER_H_
#define NETWORK_WEBSOCKET_MANAGER_H_

#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "app/debug_module.h"
#include "config/app_config.h"

/**
 * @file websocket_manager.h
 * @brief Real-time push channel (WebSocket) for UART / ADC / GPIO / system.
 *
 * Events arrive via a non-blocking queue (enqueue) so producers never block on
 * a slow or disconnected browser. The WS task drains the queue and broadcasts
 * JSON. Inbound commands are forwarded to the gateway command dispatcher.
 */
namespace RHD {

struct WsLogLine { char text[160]; };

class WebSocketManager {
public:
    static const int EV_QUEUE_LEN  = 48;
    static const int LOG_QUEUE_LEN = 32;

    bool begin();
    bool isReady() const { return _ready; }

    // Producers push here; never blocks (drops if full).
    void enqueue(const DebugEvent& ev);
    void enqueueLog(const char* line);

    uint32_t droppedEvents() const { return _dropped; }

    // Used by the static WebSockets callback (free function).
    static WebSocketManager* _self;
    void onText(uint8_t num, uint8_t* payload, size_t len);

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();
    void        broadcastEvent(const DebugEvent& ev);

    bool _ready = false;
    QueueHandle_t _evQueue  = nullptr;
    QueueHandle_t _logQueue = nullptr;
    uint32_t _dropped = 0;
    TaskHandle_t _task = nullptr;

    // WebSocketsServer is included via the .cpp (heavy header).
    void* _server = nullptr;
};

extern WebSocketManager g_ws;

} // namespace RHD

#endif // NETWORK_WEBSOCKET_MANAGER_H_
