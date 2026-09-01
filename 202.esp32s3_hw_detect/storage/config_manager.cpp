#include "config_manager.h"
#include <WiFi.h>
#include <esp_system.h>
#include <esp_mac.h>

namespace RHD {

ConfigManager g_config;

bool ConfigManager::begin() {
    _prefs.begin(AppConfig::NVS_NAMESPACE, false);

    _wifiSsid     = _prefs.getString("wifi_ssid", "");
    _wifiPassword = _prefs.getString("wifi_pass", "");

    _mqttBroker   = _prefs.getString("mqtt_broker", MqttConfig::DEFAULT_BROKER);
    _mqttPort     = _prefs.getInt("mqtt_port", MqttConfig::DEFAULT_PORT);
    _mqttUser     = _prefs.getString("mqtt_user", MqttConfig::DEFAULT_USERNAME);
    _mqttPassword = _prefs.getString("mqtt_pass", MqttConfig::DEFAULT_PASSWORD);
    _mqttKeepAlive= _prefs.getInt("mqtt_keep", MqttConfig::DEFAULT_KEEPALIVE);
    _mqttTls      = _prefs.getBool("mqtt_tls", MqttConfig::DEFAULT_TLS);

    _deviceName   = _prefs.getString("dev_name", "esp32s3-debugger");

    _uart.baud    = _prefs.getULong("uart_baud", DebugPins::UART_DEFAULT_BAUDRATE);
    _uart.dataBits= _prefs.getUChar("uart_data", DebugPins::UART_DEFAULT_DATA_BITS);
    _uart.stopBits= _prefs.getUChar("uart_stop", DebugPins::UART_DEFAULT_STOP_BITS);
    _uart.parity  = _prefs.getUChar("uart_par",  DebugPins::UART_DEFAULT_PARITY);

    _adcFsr       = _prefs.getFloat("adc_fsr", 6.144f);
    for (int i = 0; i < 4; ++i) {
        char k[16];
        snprintf(k, sizeof(k), "adc_div%d", i);
        _adcDivider[i] = _prefs.getFloat(k, 1.0f);
        snprintf(k, sizeof(k), "adc_off%d", i);
        _adcOffset[i]  = _prefs.getFloat(k, 0.0f);
    }

    _gpioOutMask   = _prefs.getUChar("gpio_out", 0x00);

    _webPassword   = _prefs.getString("web_pass", AppConfig::DEFAULT_WEB_PASSWORD);

    // If no WiFi configured, leave empty so the gateway falls back to AP mode.
    return true;
}

void ConfigManager::computeDeviceId() {
    uint8_t mac[6];
    // Works without bringing up the WiFi driver.
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        _deviceId = "esp32s3-00000000";
        return;
    }
    char id[24];
    snprintf(id, sizeof(id), "esp32s3-%02X%02X%02X%02X",
             mac[2], mac[3], mac[4], mac[5]);
    _deviceId = String(id);
    _prefs.putString("dev_id", _deviceId);
}

void ConfigManager::setWifi(const String& ssid, const String& pass) {
    _wifiSsid = ssid; _wifiPassword = pass;
}
void ConfigManager::setMqtt(const String& broker, int port, const String& user,
                            const String& pass, int keepAlive, bool tls) {
    _mqttBroker = broker; _mqttPort = port; _mqttUser = user;
    _mqttPassword = pass; _mqttKeepAlive = keepAlive; _mqttTls = tls;
}
void ConfigManager::setUart(const UartConfig& c) { _uart = c; }
void ConfigManager::setAdc(float fsr, const float divider[4], const float offset[4]) {
    _adcFsr = fsr;
    for (int i = 0; i < 4; ++i) { _adcDivider[i] = divider[i]; _adcOffset[i] = offset[i]; }
}
void ConfigManager::setGpioOutputMask(uint8_t mask) { _gpioOutMask = mask; }
void ConfigManager::setWebPassword(const String& pw) { _webPassword = pw; }

void ConfigManager::save() {
    _prefs.putString("wifi_ssid", _wifiSsid);
    _prefs.putString("wifi_pass", _wifiPassword);

    _prefs.putString("mqtt_broker", _mqttBroker);
    _prefs.putInt("mqtt_port", _mqttPort);
    _prefs.putString("mqtt_user", _mqttUser);
    _prefs.putString("mqtt_pass", _mqttPassword);
    _prefs.putInt("mqtt_keep", _mqttKeepAlive);
    _prefs.putBool("mqtt_tls", _mqttTls);

    _prefs.putString("dev_name", _deviceName);

    _prefs.putULong("uart_baud", _uart.baud);
    _prefs.putUChar("uart_data", _uart.dataBits);
    _prefs.putUChar("uart_stop", _uart.stopBits);
    _prefs.putUChar("uart_par",  _uart.parity);

    _prefs.putFloat("adc_fsr", _adcFsr);
    for (int i = 0; i < 4; ++i) {
        char k[16];
        snprintf(k, sizeof(k), "adc_div%d", i);
        _prefs.putFloat(k, _adcDivider[i]);
        snprintf(k, sizeof(k), "adc_off%d", i);
        _prefs.putFloat(k, _adcOffset[i]);
    }

    _prefs.putUChar("gpio_out", _gpioOutMask);
    _prefs.putString("web_pass", _webPassword);
}

} // namespace RHD
