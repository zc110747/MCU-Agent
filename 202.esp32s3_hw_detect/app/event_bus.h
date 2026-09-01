#ifndef APP_EVENT_BUS_H_
#define APP_EVENT_BUS_H_

#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "debug_module.h"

/**
 * @file event_bus.h
 * @brief Thread-safe event transport built on a FreeRTOS queue.
 *
 * Producers (UART / ADC / GPIO tasks, and the gateway itself for SYSTEM/ERROR)
 * push DebugEvents with push(). The EventTask is the single consumer; it fans
 * events out to WebSocket, MQTT and the log. push() never blocks: if the queue
 * is full the event is dropped and an overflow counter is incremented, so a
 * slow network client can never stall UART reception.
 */
namespace RHD {

class EventBus {
public:
    EventBus() : _queue(nullptr), _overflow(0), _dropped(0) {}

    // Create the underlying FreeRTOS queue. Must be called once before push().
    bool begin() {
        if (_queue == nullptr) {
            _queue = xQueueCreate(AppConfig::EVENT_QUEUE_LEN, sizeof(DebugEvent));
        }
        return _queue != nullptr;
    }

    // Non-blocking push. Returns false (and bumps _overflow) if the queue is full.
    bool push(const DebugEvent& ev) {
        if (_queue == nullptr) return false;
        if (xQueueSend(_queue, &ev, 0) != pdTRUE) {
            ++_overflow;
            return false;
        }
        return true;
    }

    // Blocking pop used by the EventTask (portMAX_DELAY).
    bool pop(DebugEvent& ev, TickType_t timeout = portMAX_DELAY) {
        if (_queue == nullptr) return false;
        return xQueueReceive(_queue, &ev, timeout) == pdTRUE;
    }

    QueueHandle_t handle() const { return _queue; }
    uint32_t overflowCount() const { return _overflow; }
    uint32_t droppedCount() const { return _dropped; }
    void     bumpDropped() { ++_dropped; }

private:
    QueueHandle_t _queue;
    uint32_t      _overflow;
    uint32_t      _dropped;
};

extern EventBus g_eventBus;

} // namespace RHD

#endif // APP_EVENT_BUS_H_
