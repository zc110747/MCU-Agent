#ifndef STORAGE_LOG_MANAGER_H_
#define STORAGE_LOG_MANAGER_H_

#pragma once
#include <Arduino.h>
#include "config/app_config.h"

/**
 * @file log_manager.h
 * @brief Unified, leveled logging with a bounded ring buffer for the Web UI.
 *
 * Every module logs through LOG_DEBUG/INFO/WARN/ERROR. Lines go to Serial and
 * into a fixed-size ring (no unbounded growth). An optional sink can forward
 * lines to WebSocket so the browser "Logs" view updates live.
 */
namespace RHD {

enum class LogLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

// Optional forwarding sink (set by the gateway to push logs over WebSocket).
typedef void (*LogSink)(const char* line);

class LogManager {
public:
    LogManager() : _head(0), _count(0), _sink(nullptr) {
        for (auto& line : _lines) line[0] = '\0';
    }

    void begin() { _head = 0; _count = 0; }

    void setSink(LogSink sink) { _sink = sink; }

    // printf-style logging.
    void logf(LogLevel level, const char* module, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

    // Dump the ring buffer (newest last) into `out` as newline-separated text.
    void dump(String& out) const;

    size_t count() const { return _count; }

private:
    static const size_t LINE_LEN = 160;
    char _lines[AppConfig::LOG_RING_LINES][LINE_LEN];
    size_t _head;
    size_t _count;
    LogSink _sink;
};

extern LogManager g_log;

} // namespace RHD

// --- logging macros (module is a string literal) ---
#define LOG_DEBUG(module, ...) RHD::g_log.logf(RHD::LogLevel::DEBUG, module, __VA_ARGS__)
#define LOG_INFO(module, ...)  RHD::g_log.logf(RHD::LogLevel::INFO,  module, __VA_ARGS__)
#define LOG_WARN(module, ...)  RHD::g_log.logf(RHD::LogLevel::WARN,  module, __VA_ARGS__)
#define LOG_ERROR(module, ...) RHD::g_log.logf(RHD::LogLevel::ERROR, module, __VA_ARGS__)

#endif // STORAGE_LOG_MANAGER_H_
