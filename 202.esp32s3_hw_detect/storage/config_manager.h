#ifndef STORAGE_CONFIG_MANAGER_H_
#define STORAGE_CONFIG_MANAGER_H_

#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config/pin_config.h"
#include "config/app_config.h"
#include "config/mqtt_config.h"

/**
 * @file config_manager.h
 * @brief Persistent configuration backed by Preferences (NVS).
 *
 * All user-configurable parameters live here so they survive reboots. Sensitive
 * values (WiFi/MQTT passwords) are stored in NVS but NEVER printed to logs.
 */
namespace RHD {

struct UartConfig {
    unsigned long baud     = DebugPins::UART_DEFAULT_BAUDRATE;
    uint8_t       dataBits = DebugPins::UART_DEFAULT_DATA_BITS;
    uint8_t       stopBits = DebugPins::UART_DEFAULT_STOP_BITS;
    uint8_t       parity   = DebugPins::UART_DEFAULT_PARITY; // 0 none,1 odd,2 even
};

class ConfigManager {
public:
    bool begin();

    // ---- WiFi ----
    String wifiSsid() const { return _wifiSsid; }
    String wifiPassword() const { return _wifiPassword; }
    void   setWifi(const String& ssid, const String& pass);

    // ---- MQTT ----
    String mqttBroker()   const { return _mqttBroker; }
    int    mqttPort()     const { return _mqttPort; }
    String mqttUser()     const { return _mqttUser; }
    String mqttPassword() const { return _mqttPassword; }
    int    mqttKeepAlive()const { return _mqttKeepAlive; }
    bool   mqttTls()      const { return _mqttTls; }
    void   setMqtt(const String& broker, int port, const String& user,
                   const String& pass, int keepAlive, bool tls);

    // ---- Device ----
    String deviceName() const { return _deviceName; }
    String deviceId()   const { return _deviceId; }
    void   computeDeviceId();               // from ESP32-S3 MAC
    void   setDeviceId(const String& id) { _deviceId = id; }

    // ---- UART ----
    UartConfig uartConfig() const { return _uart; }
    void       setUart(const UartConfig& c);

    // ---- ADC ----
    float adcFsrVolts() const { return _adcFsr; }
    float adcDivider(int ch) const { return (ch>=0 && ch<4) ? _adcDivider[ch] : 1.0f; }
    float adcOffset(int ch)  const { return (ch>=0 && ch<4) ? _adcOffset[ch] : 0.0f; }
    void  setAdc(float fsr, const float divider[4], const float offset[4]);

    // ---- GPIO ----
    // bitmask: bit i set => GPIO_MONITOR_PINS[i] is OUTPUT
    uint8_t gpioOutputMask() const { return _gpioOutMask; }
    void    setGpioOutputMask(uint8_t mask);

    // ---- Web auth ----
    String webPassword() const { return _webPassword; }
    void   setWebPassword(const String& pw);

    void   save();

private:
    Preferences _prefs;

    String _wifiSsid;
    String _wifiPassword;
    String _mqttBroker;
    int    _mqttPort       = MqttConfig::DEFAULT_PORT;
    String _mqttUser;
    String _mqttPassword;
    int    _mqttKeepAlive  = MqttConfig::DEFAULT_KEEPALIVE;
    bool   _mqttTls        = MqttConfig::DEFAULT_TLS;
    String _deviceName     = "esp32s3-debugger";
    String _deviceId       = "esp32s3-00000000";
    UartConfig _uart;
    float  _adcFsr         = 6.144f;  // ADS1115 ±6.144V (gain 2/3) by default
    float  _adcDivider[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
    float  _adcOffset[4]   = {0.0f, 0.0f, 0.0f, 0.0f};
    uint8_t _gpioOutMask   = 0x00;
    String _webPassword    = AppConfig::DEFAULT_WEB_PASSWORD;
};

extern ConfigManager g_config;

} // namespace RHD

#endif // STORAGE_CONFIG_MANAGER_H_
