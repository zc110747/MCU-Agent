#ifndef APP_RING_BUFFER_H_
#define APP_RING_BUFFER_H_

#pragma once
#include <Arduino.h>

/**
 * @file ring_buffer.h
 * @brief Lock-free single-producer/single-consumer byte ring buffer.
 *
 * Used by the UART monitor to absorb bursts at 115200 baud without dropping
 * bytes when the consumer (EventBus) is temporarily busy. The UART task is
 * the only producer and the only consumer of its own ring, so no lock is
 * required.
 */
class ByteRingBuffer {
public:
    explicit ByteRingBuffer(size_t capacity)
        : _cap(capacity), _buf(static_cast<uint8_t*>(malloc(capacity))) {
        _head = _tail = 0;
    }
    ~ByteRingBuffer() { free(_buf); }

    bool valid() const { return _buf != nullptr; }

    size_t available() const {
        return (_head - _tail + _cap) % _cap;
    }
    size_t freeSpace() const {
        // keep one slot empty to distinguish full from empty
        return _cap - available() - 1;
    }

    // Returns number of bytes written (may be < n if full).
    size_t write(const uint8_t* src, size_t n) {
        size_t written = 0;
        while (written < n && freeSpace() > 0) {
            _buf[_head] = src[written];
            _head = (_head + 1) % _cap;
            ++written;
        }
        return written;
    }

    // Read up to n bytes. Returns number read.
    size_t read(uint8_t* dst, size_t n) {
        size_t read = 0;
        while (read < n && available() > 0) {
            dst[read] = _buf[_tail];
            _tail = (_tail + 1) % _cap;
            ++read;
        }
        return read;
    }

    uint8_t peek(size_t offset) const {
        size_t idx = (_tail + offset) % _cap;
        return _buf[idx];
    }

    size_t availableForPeek() const { return available(); }

    void clear() { _head = _tail = 0; }

private:
    size_t   _cap;
    uint8_t* _buf;
    size_t   _head;
    size_t   _tail;
};

#endif // APP_RING_BUFFER_H_
