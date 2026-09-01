#ifndef APP_PIN_MANAGER_H_
#define APP_PIN_MANAGER_H_

#pragma once
#include <Arduino.h>
#include "config/pin_config.h"

/**
 * @file pin_manager.h
 * @brief Runtime-enforced GPIO resource manager.
 *
 * No module may use a GPIO without first claiming it here. Claims are checked
 * against the reserved pins (USB D-/D+ and the RUN LED) and against each other.
 * Overlaps are rejected so the same pin can never be driven by two modules.
 */
namespace DebugPins {

struct PinClaim {
    int       pin;
    const char* owner;   // module name
    bool      output;   // direction hint (informational)
};

class PinManager {
public:
    static const int MAX_CLAIMS = 32;

    // Claim a pin for a module. Returns false if reserved or already taken.
    bool claim(int pin, const char* owner, bool output = false);

    // Release a previously claimed pin.
    void release(int pin);

    // Is this pin permanently reserved (never claimable)?
    bool isReserved(int pin) const;

    // Is this pin already claimed by some module?
    bool isClaimed(int pin) const;

    // Does the claim table already contain this (pin,owner)?
    bool isOwnedBy(int pin, const char* owner) const;

    // Print the final GPIO allocation table to Serial.
    void printAllocation() const;

    int claimCount() const { return _count; }
    const PinClaim* claims() const { return _claims; }

private:
    PinClaim _claims[MAX_CLAIMS];
    int      _count = 0;
};

// Global singleton
extern PinManager g_pinManager;

} // namespace DebugPins

#endif // APP_PIN_MANAGER_H_
