#ifndef NETWORK_WIFI_MANAGER_H_
#define NETWORK_WIFI_MANAGER_H_

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "config/app_config.h"

/**
 * @file wifi_manager.h
 * @brief WiFi STA with AP fallback and auto-reconnect.
 *
 * Normal operation is STA. If no SSID is configured or STA association fails,
 * the device brings up an AP (ESP32S3-Debugger-XXXX @ 192.168.4.1) so the user
 * can configure WiFi from the browser. STA auto-reconnects if the link drops.
 */
namespace RHD {

class WiFiManager {
public:
    enum class Mode { NONE, STA, AP };

    bool begin();
    bool connected() const { return WiFi.isConnected(); }
    int  rssi() const { return WiFi.RSSI(); }
    String ip() const { return WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString(); }
    Mode mode() const { return _mode; }
    String modeStr() const {
        return _mode == Mode::STA ? "sta" : (_mode == Mode::AP ? "ap" : "none");
    }
    String apSsid() const { return _apSsid; }
    String apIp() const { return WiFi.softAPIP().toString(); }
    int   stationCount() const { return WiFi.softAPgetStationNum(); }

    // Apply new STA credentials (saved by caller) and (re)connect.
    void applySta(const String& ssid, const String& pass);

private:
    void startAP();
    void connectSta(const String& ssid, const String& pass);

    Mode   _mode = Mode::NONE;
    String _apSsid;
};

} // namespace RHD

#endif // NETWORK_WIFI_MANAGER_H_
