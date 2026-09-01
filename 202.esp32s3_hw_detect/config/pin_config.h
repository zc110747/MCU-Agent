#ifndef CONFIG_PIN_CONFIG_H_
#define CONFIG_PIN_CONFIG_H_

#pragma once
#include <Arduino.h>

/**
 * @file pin_config.h
 * @brief Centralized GPIO resource management for the ESP32-S3 Remote Hardware Debugger.
 *
 * HARD RULES (enforced at runtime by PinManager):
 *   - GPIO19 / GPIO20 are USB D-/D+ and MUST NOT be used as general GPIO.
 *   - GPIO48 is the on-board RUN / status LED (user-confirmed). It is allocated
 *     by the system as RUN_LED and must not be reclaimed by any debug module.
 *   - Every functional pin is declared here. No module may hardcode a raw GPIO
 *     number. Modules call PinManager::claim() at init; overlaps are rejected.
 *
 * All GPIO choices below target a generic ESP32-S3 (N16R8) module and avoid:
 *   - Strapping / boot pins (0, 45, 46, 3 on some boards)
 *   - Flash / PSRAM pins (26..32)
 *   - USB pins (19, 20) and the RUN LED (48)
 * If you are unsure about a specific board, leave the pin in THIS file as the
 * single source of truth and adjust only here.
 */
namespace DebugPins {

// ---- Reserved / dedicated pins (never available to debug modules) ----
constexpr int RESERVED_USB_DM = 19;  // USB D- (do not touch)
constexpr int RESERVED_USB_DP = 20;  // USB D+ (do not touch)
constexpr int RUN_LED        = 48;  // Board RUN / status LED (user-confirmed)
constexpr int RESERVED_COUNT = 3;

// Pins that must never be claimed by any module (checked by PinManager).
constexpr int RESERVED_PINS[RESERVED_COUNT] = { RESERVED_USB_DM, RESERVED_USB_DP, RUN_LED };

// ---- UART Debug Port (to Target MCU) ----
// ESP32-S3 RX <- Target MCU TX ; ESP32-S3 TX -> Target MCU RX
constexpr int UART_RX = 17;
constexpr int UART_TX = 18;

// ---- External ADC (ADS1115) over I2C ----
constexpr int ADC_I2C_SDA    = 8;
constexpr int ADC_I2C_SCL    = 9;
constexpr uint8_t ADC_I2C_ADDR = 0x48; // ADS1115 ADDR pin -> GND

// ---- GPIO Monitor (digital in / out) ----
// Set as input by default; can be switched to output via Web/MQTT.
constexpr int GPIO_MONITOR_PINS[] = { 4, 5, 6, 7 };
constexpr int GPIO_MONITOR_COUNT  = 4;

// ---- UART default parameters ----
constexpr unsigned long UART_DEFAULT_BAUDRATE = 115200;
constexpr uint8_t      UART_DEFAULT_DATA_BITS  = 8;
constexpr uint8_t      UART_DEFAULT_STOP_BITS  = 1;  // 1 or 2
constexpr uint8_t      UART_DEFAULT_PARITY     = 0;  // 0 none, 1 odd, 2 even

// ---- UART RX driver ring buffer (bytes) ----
constexpr size_t UART_RX_BUFFER_SIZE = 4096;

// ---- I2C bus frequency ----
constexpr uint32_t ADC_I2C_FREQ = 400000; // ADS1115 supports up to 400kHz

} // namespace DebugPins

#endif // CONFIG_PIN_CONFIG_H_
