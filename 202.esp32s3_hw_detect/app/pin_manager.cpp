#include "pin_manager.h"
#include <Arduino.h>

namespace DebugPins {

PinManager g_pinManager;

bool PinManager::claim(int pin, const char* owner, bool output) {
    if (isReserved(pin)) {
        return false; // never claimable
    }
    if (isClaimed(pin)) {
        return false; // already taken by another module
    }
    if (_count >= MAX_CLAIMS) {
        return false;
    }
    _claims[_count].pin    = pin;
    _claims[_count].owner  = owner;
    _claims[_count].output = output;
    ++_count;
    return true;
}

void PinManager::release(int pin) {
    for (int i = 0; i < _count; ++i) {
        if (_claims[i].pin == pin) {
            for (int j = i; j < _count - 1; ++j) {
                _claims[j] = _claims[j + 1];
            }
            --_count;
            return;
        }
    }
}

bool PinManager::isReserved(int pin) const {
    for (int i = 0; i < RESERVED_COUNT; ++i) {
        if (RESERVED_PINS[i] == pin) return true;
    }
    return false;
}

bool PinManager::isClaimed(int pin) const {
    for (int i = 0; i < _count; ++i) {
        if (_claims[i].pin == pin) return true;
    }
    return false;
}

bool PinManager::isOwnedBy(int pin, const char* owner) const {
    for (int i = 0; i < _count; ++i) {
        if (_claims[i].pin == pin && strcmp(_claims[i].owner, owner) == 0) {
            return true;
        }
    }
    return false;
}

void PinManager::printAllocation() const {
    log_printf("\n--- GPIO Resource Allocation ---\n");
    log_printf("Reserved (never claimable):\n");
    for (int i = 0; i < RESERVED_COUNT; ++i) {
        log_printf("  GPIO%-2d : RESERVED (%s)\n",
                   RESERVED_PINS[i],
                   (RESERVED_PINS[i] == RUN_LED) ? "RUN LED"
                       : (RESERVED_PINS[i] == RESERVED_USB_DM) ? "USB D-"
                       : "USB D+");
    }
    log_printf("Claimed by modules:\n");
    if (_count == 0) {
        log_printf("  (none)\n");
    }
    for (int i = 0; i < _count; ++i) {
        log_printf("  GPIO%-2d : %-10s [%s]\n",
                   _claims[i].pin, _claims[i].owner,
                   _claims[i].output ? "output" : "input");
    }
    log_printf("--------------------------------\n\n");
}

} // namespace DebugPins
