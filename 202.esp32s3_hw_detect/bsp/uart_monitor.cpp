#include "bsp/uart_monitor.h"
#include "app/event_bus.h"
#include "app/pin_manager.h"
#include "storage/config_manager.h"
#include "storage/log_manager.h"

namespace RHD {

// ESP32 Arduino encodes framing in a 32-bit config word (see HardwareSerial.h).
uint32_t UartMonitor::buildConfig(uint8_t dataBits, uint8_t stopBits, uint8_t parity) {
    uint8_t d = (dataBits < 5 || dataBits > 8) ? 8 : dataBits;
    uint8_t p = (parity > 2) ? 0 : parity;

    if (stopBits == 2) {
        if (p == 1) { switch (d) { case 5: return SERIAL_5O2; case 6: return SERIAL_6O2; case 7: return SERIAL_7O2; default: return SERIAL_8O2; } }
        if (p == 2) { switch (d) { case 5: return SERIAL_5E2; case 6: return SERIAL_6E2; case 7: return SERIAL_7E2; default: return SERIAL_8E2; } }
        switch (d) { case 5: return SERIAL_5N2; case 6: return SERIAL_6N2; case 7: return SERIAL_7N2; default: return SERIAL_8N2; }
    }
    if (p == 1) { switch (d) { case 5: return SERIAL_5O1; case 6: return SERIAL_6O1; case 7: return SERIAL_7O1; default: return SERIAL_8O1; } }
    if (p == 2) { switch (d) { case 5: return SERIAL_5E1; case 6: return SERIAL_6E1; case 7: return SERIAL_7E1; default: return SERIAL_8E1; } }
    switch (d) { case 5: return SERIAL_5N1; case 6: return SERIAL_6N1; case 7: return SERIAL_7N1; default: return SERIAL_8N1; }
}

bool UartMonitor::begin() {
    if (!DebugPins::g_pinManager.claim(DebugPins::UART_RX, "UART", false)) {
        LOG_ERROR("UART", "cannot claim RX GPIO%d", DebugPins::UART_RX);
        return false;
    }
    if (!DebugPins::g_pinManager.claim(DebugPins::UART_TX, "UART", true)) {
        LOG_ERROR("UART", "cannot claim TX GPIO%d", DebugPins::UART_TX);
        return false;
    }

    _ring = new ByteRingBuffer(DebugPins::UART_RX_BUFFER_SIZE);
    if (!_ring || !_ring->valid()) {
        LOG_ERROR("UART", "ring buffer alloc failed (%u B)", (unsigned)DebugPins::UART_RX_BUFFER_SIZE);
        return false;
    }

    UartConfig uc = g_config.uartConfig();
    _baud = uc.baud;
    _serial->setRxBufferSize(1024);
    _serial->begin(_baud, buildConfig(uc.dataBits, uc.stopBits, uc.parity),
                   DebugPins::UART_RX, DebugPins::UART_TX);
    _ready = true;

    LOG_INFO("UART", "monitor ready RX=GPIO%d TX=GPIO%d %lu %u%c%u",
             DebugPins::UART_RX, DebugPins::UART_TX, _baud, uc.dataBits,
             uc.parity == 0 ? 'N' : (uc.parity == 1 ? 'O' : 'E'),
             (unsigned)uc.stopBits);

    xTaskCreatePinnedToCore(&UartMonitor::taskTrampoline, "uart_task",
                            AppConfig::UART_TASK_STACK, this,
                            AppConfig::UART_TASK_PRIO, &_task,
                            AppConfig::UART_TASK_CORE);
    return true;
}

void UartMonitor::configure(unsigned long baud, uint8_t dataBits, uint8_t stopBits, uint8_t parity) {
    _baud = baud;
    if (!_ready) return;
    _serial->end();
    if (_ring) _ring->clear();
    _lineLen = 0;
    _serial->begin(baud, buildConfig(dataBits, stopBits, parity),
                   DebugPins::UART_RX, DebugPins::UART_TX);
}

void UartMonitor::send(const String& text) {
    if (!_ready || text.length() == 0) return;
    _serial->print(text);

    DebugEvent ev;
    ev.type      = DebugEventType::UART_TX;
    ev.timestamp = millis();
    ev.encoding  = 0;
    size_t n = text.length();
    if (n > AppConfig::EVENT_DATA_MAX - 1) n = AppConfig::EVENT_DATA_MAX - 1;
    memcpy(ev.data, text.c_str(), n);
    ev.data[n] = '\0';
    ev.length = (uint16_t)n;
    g_eventBus.push(ev);
}

void UartMonitor::taskTrampoline(void* arg) {
    static_cast<UartMonitor*>(arg)->taskLoop();
}

void UartMonitor::taskLoop() {
    uint8_t tmp[256];
    while (true) {
        if (_ready) {
            int avail = _serial->available();
            if (avail > 0) {
                if (avail > (int)sizeof(tmp)) avail = (int)sizeof(tmp);
                int n = _serial->readBytes(tmp, (size_t)avail);
                if (n > 0) {
                    size_t w = _ring->write(tmp, (size_t)n);
                    if (w < (size_t)n) _dropped += ((size_t)n - w);
                    _lastByte = millis();
                }
            }
            frameAndPush();
        }
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

void UartMonitor::pushFrame(const char* text, size_t len, bool hex) {
    DebugEvent ev;
    ev.type      = DebugEventType::UART_RX;
    ev.timestamp = millis();
    ev.encoding  = hex ? 1 : 0;
    if (len > AppConfig::EVENT_DATA_MAX - 1) len = AppConfig::EVENT_DATA_MAX - 1;
    memcpy(ev.data, text, len);
    ev.data[len] = '\0';
    ev.length = (uint16_t)len;
    g_eventBus.push(ev);
}

void UartMonitor::frameAndPush() {
    if (!_ring) return;

    if (_hex) {
        // Hex mode: emit whatever is buffered in chunks so the UI stays live.
        static const size_t HEX_BLOCK = 16;
        uint8_t blk[HEX_BLOCK];
        while (_ring->available() > 0) {
            size_t n = _ring->read(blk, HEX_BLOCK);
            if (n == 0) break;
            char out[HEX_BLOCK * 3 + 1];
            size_t o = 0;
            for (size_t i = 0; i < n; ++i) {
                o += (size_t)snprintf(out + o, sizeof(out) - o, "%02X ", blk[i]);
            }
            if (o > 0 && out[o - 1] == ' ') out[--o] = '\0';
            pushFrame(out, o, true);
        }
        return;
    }

    // Text mode: line oriented, with an idle flush so unterminated lines show up.
    uint8_t b;
    bool flushed = false;
    while (_ring->read(&b, 1) == 1) {
        if (b == '\n' || b == '\r') {
            if (_lineLen > 0) { pushFrame(_line, _lineLen, false); _lineLen = 0; flushed = true; }
        } else {
            _line[_lineLen++] = (char)b;
            if (_lineLen >= sizeof(_line) - 1) {
                pushFrame(_line, _lineLen, false);
                _lineLen = 0;
                flushed = true;
            }
        }
    }
    if (!flushed && _lineLen > 0 && (millis() - _lastByte > 30)) {
        pushFrame(_line, _lineLen, false);
        _lineLen = 0;
    }
}

} // namespace RHD
