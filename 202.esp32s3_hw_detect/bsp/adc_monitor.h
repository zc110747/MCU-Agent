#ifndef DEBUG_ADC_MONITOR_H_
#define DEBUG_ADC_MONITOR_H_

#pragma once
#include <Arduino.h>
#include "app/debug_module.h"
#include "config/pin_config.h"

/**
 * @file adc_monitor.h
 * @brief Periodic 4-channel voltage sampling with an abstract ADC backend.
 *
 * The core only talks to `ExternalAdc`, so swapping the converter (ADS1115 ->
 * MCP3208 -> ...) means writing one subclass. Two backends ship today:
 *   - Ads1115Adc : external 16-bit delta-sigma over I2C (4 channels, +/-6.144V)
 *   - EspAdc     : fallback to the ESP32-S3 internal ADC1 (GPIO1/2)
 *
 * Samples are published as ADC_SAMPLE DebugEvents and cached so the WebSocket
 * state snapshot can report the latest value without touching the I2C bus.
 */
namespace RHD {

// ---------------------------------------------------------------- ExternalAdc
class ExternalAdc {
public:
    virtual ~ExternalAdc() = default;

    virtual bool       begin() = 0;
    virtual bool       available() const = 0;
    virtual const char* name() const = 0;
    virtual uint8_t    channelCount() const = 0;

    // `raw`     : converter counts (displayed in the UI)
    // `volts`   : voltage BEFORE the per-channel divider/offset is applied
    virtual bool readChannel(uint8_t ch, uint32_t& raw, float& volts) = 0;

    // Full-scale range in volts (ADS1115 only; ignored by the internal ADC).
    virtual void setFsr(float fsr) { (void)fsr; }
};

// --------------------------------------------------------------- ADS1115 (I2C)
class Ads1115Adc : public ExternalAdc {
public:
    bool        begin() override;
    bool        available() const override { return _present; }
    const char* name() const override { return "ADS1115"; }
    uint8_t     channelCount() const override { return 4; }
    bool        readChannel(uint8_t ch, uint32_t& raw, float& volts) override;
    void        setFsr(float fsr) override;

private:
    bool   writeReg(uint8_t reg, uint16_t value);
    bool   readReg(uint8_t reg, uint16_t& value);
    uint8_t pgaFor(float fsr) const;

    bool  _present = false;
    float _fsr     = 6.144f;
};

// ------------------------------------------------- ESP32-S3 internal ADC1 (2ch)
class EspAdc : public ExternalAdc {
public:
    bool        begin() override;
    bool        available() const override { return _ready; }
    const char* name() const override { return "internal-adc1"; }
    uint8_t     channelCount() const override { return (uint8_t)DebugPins::INTERNAL_ADC_COUNT; }
    bool        readChannel(uint8_t ch, uint32_t& raw, float& volts) override;

private:
    bool _ready = false;
};

// ------------------------------------------------------------------- AdcMonitor
class AdcMonitor {
public:
    bool begin();

    // Apply FSR (external ADC) + per-channel divider/offset.
    void configure(float fsr, const float divider[4], const float offset[4]);

    // Sampling period (ms). 0 disables the periodic task.
    void     setInterval(uint32_t ms) { _intervalMs = ms; }
    uint32_t interval() const { return _intervalMs; }

    // Latest cached sample (post divider/offset). Safe to call from any task.
    void latest(uint16_t raw[4], float volts[4]) const;

    bool        isReady() const   { return _ready; }
    uint32_t    dropCount() const { return _dropped; }
    const char* adcSource() const;

    // Direct backend access (used by the `adc_read` command).
    ExternalAdc* adc() { return _adc; }

    // Read all channels once and push ADC_SAMPLE events.
    bool sampleOnce();

private:
    static void taskTrampoline(void* arg);
    void        taskLoop();

    Ads1115Adc*  _ads  = nullptr;
    EspAdc*      _esp  = nullptr;
    ExternalAdc* _adc  = nullptr;

    float    _fsr         = 6.144f;
    float    _divider[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
    float    _offset[4]   = {0.0f, 0.0f, 0.0f, 0.0f};
    uint16_t _lastRaw[4]  = {0, 0, 0, 0};
    float    _lastVolt[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    bool         _ready      = false;
    uint32_t     _dropped    = 0;
    uint32_t     _intervalMs = AppConfig::ADC_DEFAULT_INTERVAL_MS;
    TaskHandle_t _task       = nullptr;
};

} // namespace RHD

#endif // DEBUG_ADC_MONITOR_H_
