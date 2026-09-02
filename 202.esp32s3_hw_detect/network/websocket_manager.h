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
    bool hasClients() const { return _clients > 0; }
    uint8_t clientCount() const { return _clients; }

    // Producers push here; never blocks (drops if full).
    void enqueue(const DebugEvent& ev);
    void enqueueLog(const char* line);

    uint32_t droppedEvents() const { return _dropped; }

    // Used by the static WebSockets callback (free function).
    static WebSocketManager* _self;
    void onText(uint8_t num, uint8_t* payload, size_t len);
    void onConnect();
    void onDisconnect();

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();
    void        broadcastEvent(const DebugEvent& ev);

    // Periodic full Interface snapshot: GPIO levels, WS2812 live state,
    // PWM parameters and the latest ADC values. Built on demand (no queueing)
    // so the browser never has to poll for hardware state.
    void        broadcastState();

    bool _ready = false;
    QueueHandle_t _evQueue  = nullptr;
    QueueHandle_t _logQueue = nullptr;
    uint32_t _dropped = 0;
    uint32_t _lastState = 0;
    volatile uint8_t _clients = 0;
    TaskHandle_t _task = nullptr;

    // WebSocketsServer is included via the .cpp (heavy header).
    void* _server = nullptr;
};

extern WebSocketManager g_ws;

} // namespace RHD

#endif // NETWORK_WEBSOCKET_MANAGER_H_
