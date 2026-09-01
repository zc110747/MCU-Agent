#include "wifi_manager.h"
#include "storage/config_manager.h"
#include "storage/log_manager.h"
#include <WiFi.h>

namespace RHD {

bool WiFiManager::begin() {
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);

    String ssid = g_config.wifiSsid();
    String pass = g_config.wifiPassword();

    if (ssid.length() == 0) {
        LOG_WARN("WiFi", "no SSID configured -> AP mode");
        startAP();
        return true;
    }
    connectSta(ssid, pass);
    return true;
}

void WiFiManager::connectSta(const String& ssid, const String& pass) {
    LOG_INFO("WiFi", "connecting to '%s'...", ssid.c_str());
    WiFi.mode(WIFI_MODE_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t t0 = millis();
    while (millis() - t0 < AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
        if (WiFi.isConnected()) break;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (WiFi.isConnected()) {
        _mode = Mode::STA;
        LOG_INFO("WiFi", "connected, IP=%s", WiFi.localIP().toString().c_str());
    } else {
        LOG_WARN("WiFi", "STA connect failed -> AP fallback");
        startAP();
    }
}

void WiFiManager::startAP() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X",
             AppConfig::AP_SSID_PREFIX, mac[4], mac[5]);
    _apSsid = String(ssid);

    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAPConfig(AppConfig::AP_IP, AppConfig::AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ssid, AppConfig::AP_PASSWORD);
    _mode = Mode::AP;
    LOG_INFO("WiFi", "AP '%s' up @ %s", ssid, WiFi.softAPIP().toString().c_str());
}

void WiFiManager::applySta(const String& ssid, const String& pass) {
    LOG_INFO("WiFi", "applying new STA '%s'", ssid.c_str());
    connectSta(ssid, pass);
}

} // namespace RHD
