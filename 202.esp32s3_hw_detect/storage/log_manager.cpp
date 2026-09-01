#include "log_manager.h"
#include <stdarg.h>
#include <stdio.h>

namespace RHD {

LogManager g_log;

static const char* levelTag(LogLevel l) {
    switch (l) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "?";
    }
}

void LogManager::logf(LogLevel level, const char* module, const char* fmt, ...) {
    char buf[LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    uint32_t ts = millis();
    char line[LINE_LEN];
    int n = snprintf(line, sizeof(line), "[%u][%s][%s] %s",
                     ts, levelTag(level), module, buf);
    if (n < 0) line[0] = '\0';

    // Serial
    log_printf("%s\n", line);

    // Ring buffer (overwrite oldest)
    strncpy(_lines[_head], line, LINE_LEN - 1);
    _lines[_head][LINE_LEN - 1] = '\0';
    _head = (_head + 1) % AppConfig::LOG_RING_LINES;
    if (_count < AppConfig::LOG_RING_LINES) ++_count;

    if (_sink) _sink(line);
}

void LogManager::dump(String& out) const {
    out = "";
    // oldest -> newest
    size_t start = (_count < AppConfig::LOG_RING_LINES)
                       ? (_head - _count + AppConfig::LOG_RING_LINES) % AppConfig::LOG_RING_LINES
                       : _head;
    for (size_t i = 0; i < _count; ++i) {
        size_t idx = (start + i) % AppConfig::LOG_RING_LINES;
        out += _lines[idx];
        out += '\n';
    }
}

} // namespace RHD
