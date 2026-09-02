#include "bsp/adc_monitor.h"
#include "app/event_bus.h"
#include "app/pin_manager.h"
#include "storage/config_manager.h"
#include "storage/log_manager.h"
#include <Wire.h>

namespace RHD {

// =============================================================== ADS1115 (I2C)
// Register map
static const uint8_t ADS_REG_CONVERT   = 0x00;
static const uint8_t ADS_REG_CONFIG    = 0x01;

// Config bit fields
//   [15]    OS   : 1 = start a single conversion
//   [14:12] MUX  : 100..111 = AIN0..AIN3 vs GND
//   [11:9]  PGA  : full scale range
//   [8]     MODE : 1 = single-shot
//   [7:5]   DR   : data rate (111 = 860 SPS)
//   [4:0]   comparator disabled (0b00011)
static const uint16_t ADS_OS_START     = 0x8000;
static const uint16_t ADS_MODE_SINGLE  = 0x0100;
static const uint16_t ADS_DR_860SPS    = 0x00E0;
static const uint16_t ADS_COMP_DISABLE = 0x0003;

bool Ads1115Adc::writeReg(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(DebugPins::ADC_I2C_ADDR);
    Wire.write(reg);
    Wire.write((uint8_t)(value >> 8));
    Wire.write((uint8_t)(value & 0xFF));
    return Wire.endTransmission() == 0;
}

bool Ads1115Adc::readReg(uint8_t reg, uint16_t& value) {
    Wire.beginTransmission(DebugPins::ADC_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)DebugPins::ADC_I2C_ADDR, (uint8_t)2) != 2) return false;
    value = (uint16_t)((Wire.read() << 8) | Wire.read());
    return true;
}

uint8_t Ads1115Adc::pgaFor(float fsr) const {
    // ADS1115 PGA table (gain 2/3 .. 16)
    if (fsr <= 0.256f) return 0x05;   // +/-0.256V
    if (fsr <= 0.512f) return 0x04;   // +/-0.512V
    if (fsr <= 1.024f) return 0x03;   // +/-1.024V
    if (fsr <= 2.048f) return 0x02;   // +/-2.048V
    if (fsr <= 4.096f) return 0x01;   // +/-4.096V
    return 0x00;                      // +/-6.144V
}

float lsbForPga(uint8_t pga) {
    // Full-scale voltage / 2^15
    switch (pga) {
        case 0x05: return 0.256f / 32768.0f;
        case 0x04: return 0.512f / 32768.0f;
        case 0x03: return 1.024f / 32768.0f;
        case 0x02: return 2.048f / 32768.0f;
        case 0x01: return 4.096f / 32768.0f;
        default:   return 6.144f / 32768.0f;
    }
}

void Ads1115Adc::setFsr(float fsr) {
    if (fsr <= 0.0f) return;
    _fsr = fsr;
}

bool Ads1115Adc::begin() {
    Wire.begin(DebugPins::ADC_I2C_SDA, DebugPins::ADC_I2C_SCL);
    Wire.setClock(DebugPins::ADC_I2C_FREQ);

    Wire.beginTransmission(DebugPins::ADC_I2C_ADDR);
    _present = (Wire.endTransmission() == 0);
    if (!_present) return false;

    // One dummy conversion to validate the datapath.
    uint32_t raw; float v;
    _present = readChannel(0, raw, v);
    return _present;
}

bool Ads1115Adc::readChannel(uint8_t ch, uint32_t& raw, float& volts) {
    if (!_present || ch >= 4) return false;

    uint8_t pga = pgaFor(_fsr);
    uint16_t cfg = ADS_OS_START
                 | (uint16_t)((0x04 + ch) << 12)   // AINx vs GND
                 | (uint16_t)(pga << 9)
                 | ADS_MODE_SINGLE
                 | ADS_DR_860SPS
                 | ADS_COMP_DISABLE;

    if (!writeReg(ADS_REG_CONFIG, cfg)) return false;

    // 860 SPS -> ~1.2 ms per conversion; poll the OS bit instead of a blind delay.
    uint32_t deadline = millis() + 20;
    uint16_t status = 0;
    do {
        delayMicroseconds(200);
        if (!readReg(ADS_REG_CONFIG, status)) return false;
    } while ((status & ADS_OS_START) == 0 && (int32_t)(millis() - deadline) < 0);

    uint16_t value = 0;
    if (!readReg(ADS_REG_CONVERT, value)) return false;

    raw = (uint32_t)value;
    // Two's complement -> volts, before divider/offset.
    int16_t s = (int16_t)value;
    volts = (float)s * lsbForPga(pga);
    return true;
}

// ======================================================== ESP32-S3 internal ADC1
bool EspAdc::begin() {
    if (!DebugPins::g_pinManager.claim(DebugPins::INTERNAL_ADC_PINS[0], "ADC", false)) {
        LOG_ERROR("ADC", "cannot claim internal ADC GPIO%d", DebugPins::INTERNAL_ADC_PINS[0]);
        return false;
    }
    if (!DebugPins::g_pinManager.claim(DebugPins::INTERNAL_ADC_PINS[1], "ADC", false)) {
        LOG_ERROR("ADC", "cannot claim internal ADC GPIO%d", DebugPins::INTERNAL_ADC_PINS[1]);
        return false;
    }
    analogReadResolution(12);
    for (int i = 0; i < DebugPins::INTERNAL_ADC_COUNT; ++i) {
        analogSetPinAttenuation(DebugPins::INTERNAL_ADC_PINS[i], ADC_11db);
    }
    _ready = true;
    LOG_INFO("ADC", "internal ADC1 fallback on GPIO%d/GPIO%d (0..3.3V, 12-bit)",
             DebugPins::INTERNAL_ADC_PINS[0], DebugPins::INTERNAL_ADC_PINS[1]);
    return true;
}

bool EspAdc::readChannel(uint8_t ch, uint32_t& raw, float& volts) {
    if (!_ready || ch >= (uint8_t)DebugPins::INTERNAL_ADC_COUNT) return false;
    int pin = DebugPins::INTERNAL_ADC_PINS[ch];
    int counts = analogRead(pin);
    if (counts < 0) counts = 0;
    raw = (uint32_t)counts;
    // Prefer the calibrated mV path; fall back to the nominal 3.3V reference.
    int mv = analogReadMilliVolts(pin);
    volts = (mv > 0) ? (mv / 1000.0f) : ((float)counts * 3.3f / 4095.0f);
    return true;
}

// ==================================================================== AdcMonitor
bool AdcMonitor::begin() {
    if (!DebugPins::g_pinManager.claim(DebugPins::ADC_I2C_SDA, "ADC", false)) {
        LOG_ERROR("ADC", "cannot claim I2C SDA GPIO%d", DebugPins::ADC_I2C_SDA);
        return false;
    }
    if (!DebugPins::g_pinManager.claim(DebugPins::ADC_I2C_SCL, "ADC", true)) {
        LOG_ERROR("ADC", "cannot claim I2C SCL GPIO%d", DebugPins::ADC_I2C_SCL);
        return false;
    }

    _fsr = g_config.adcFsrVolts();
    for (int i = 0; i < 4; ++i) {
        _divider[i] = g_config.adcDivider(i);
        _offset[i]  = g_config.adcOffset(i);
    }

    _ads = new Ads1115Adc();
    _ads->setFsr(_fsr);
    if (_ads->begin()) {
        _adc = _ads;
        LOG_INFO("ADC", "ADS1115 detected @0x%02X on SDA=GPIO%d SCL=GPIO%d",
                 DebugPins::ADC_I2C_ADDR, DebugPins::ADC_I2C_SDA, DebugPins::ADC_I2C_SCL);
    } else {
        LOG_WARN("ADC", "no ADS1115 on I2C - falling back to internal ADC1");
        delete _ads; _ads = nullptr;
        _esp = new EspAdc();
        if (_esp->begin()) {
            _adc = _esp;
        } else {
            delete _esp; _esp = nullptr;
            LOG_ERROR("ADC", "no ADC backend available");
            return false;
        }
    }

    _ready = true;
    xTaskCreatePinnedToCore(&AdcMonitor::taskTrampoline, "adc_task",
                            AppConfig::ADC_TASK_STACK, this,
                            AppConfig::ADC_TASK_PRIO, &_task,
                            AppConfig::ADC_TASK_CORE);
    return true;
}

void AdcMonitor::configure(float fsr, const float divider[4], const float offset[4]) {
    _fsr = (fsr > 0.0f) ? fsr : 6.144f;
    for (int i = 0; i < 4; ++i) {
        _divider[i] = divider[i];
        _offset[i]  = offset[i];
    }
    if (_adc) _adc->setFsr(_fsr);
}

void AdcMonitor::latest(uint16_t raw[4], float volts[4]) const {
    for (int i = 0; i < 4; ++i) {
        raw[i]   = _lastRaw[i];
        volts[i] = _lastVolt[i];
    }
}

const char* AdcMonitor::adcSource() const {
    return _adc ? _adc->name() : "none";
}

bool AdcMonitor::sampleOnce() {
    if (!_ready || !_adc) return false;

    bool any = false;
    uint8_t n = _adc->channelCount();
    if (n > AppConfig::ADC_CHANNEL_COUNT) n = AppConfig::ADC_CHANNEL_COUNT;

    for (uint8_t ch = 0; ch < n; ++ch) {
        uint32_t raw = 0;
        float    pre = 0.0f;
        if (!_adc->readChannel(ch, raw, pre)) continue;

        float v = pre * _divider[ch] + _offset[ch];
        _lastRaw[ch]  = (uint16_t)(raw & 0xFFFF);
        _lastVolt[ch] = v;
        any = true;

        DebugEvent ev;
        ev.type      = DebugEventType::ADC_SAMPLE;
        ev.timestamp = millis();
        ev.channel   = ch;
        ev.raw       = (uint16_t)(raw & 0xFFFF);
        ev.voltage   = v;
        if (!g_eventBus.push(ev)) ++_dropped;
    }
    return any;
}

void AdcMonitor::taskTrampoline(void* arg) {
    static_cast<AdcMonitor*>(arg)->taskLoop();
}

void AdcMonitor::taskLoop() {
    uint32_t last = 0;
    while (true) {
        if (_intervalMs > 0 && (millis() - last >= _intervalMs)) {
            last = millis();
            sampleOnce();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

} // namespace RHD
