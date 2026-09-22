/**
 * @file net_service.cpp
 * @brief Network status for the Settings page (see the header for scope).
 */

#include "net_service.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "net";

namespace services {

namespace {

WifiState s_state = WifiState::Off;
char      s_ssid[33] = {0};
char      s_ip[16] = "0.0.0.0";
WifiAp    s_aps[kWifiMaxAps] = {};
size_t    s_ap_count = 0;
bool      s_scanning = false;

}  // namespace

esp_err_t net_init()
{
    /* The WiFi stack is deliberately not started here: see the header.  The
     * service still initialises so that callers have a defined state to read
     * instead of "Off" meaning both "not asked" and "asked and failed". */
    s_state = WifiState::NotConfigured;
    snprintf(s_ssid, sizeof(s_ssid), "%s", "");
    snprintf(s_ip, sizeof(s_ip), "%s", "0.0.0.0");
    s_ap_count = 0;

    ESP_LOGI(TAG, "status: %s (WiFi bring-up is a separate increment)",
             net_wifi_state_text(s_state));
    return ESP_OK;
}

WifiState net_wifi_state()
{
    return s_state;
}

const char *net_wifi_state_text(WifiState st)
{
    switch (st) {
    case WifiState::Off:           return "off";
    case WifiState::NotConfigured: return "not configured";
    case WifiState::Idle:          return "idle";
    case WifiState::Connected:     return "connected";
    case WifiState::Error:         return "error";
    case WifiState::Unsupported:   return "unsupported";
    default:                       return "?";
    }
}

const char *net_ssid()
{
    return s_ssid;
}

const char *net_ip()
{
    return s_ip;
}

esp_err_t net_scan_start()
{
    s_scanning = false;
    ESP_LOGW(TAG, "scan requested, but this build has no WiFi stack");
    return ESP_ERR_NOT_SUPPORTED;
}

bool net_scan_busy()
{
    return s_scanning;
}

const WifiAp *net_scan_results(size_t *count)
{
    if (count != nullptr) {
        *count = s_ap_count;
    }
    return s_aps;
}

size_t net_scan_copy(WifiAp *out, size_t max)
{
    if (out == nullptr || max == 0) {
        return 0;
    }
    const size_t n = (s_ap_count < max) ? s_ap_count : max;
    memcpy(out, s_aps, n * sizeof(WifiAp));
    return n;
}

int net_rssi_bars(int8_t rssi)
{
    /* -50 dBm and better is full scale; -90 and worse is nothing. */
    if (rssi >= -50) return 4;
    if (rssi >= -60) return 3;
    if (rssi >= -70) return 2;
    if (rssi >= -85) return 1;
    return 0;
}

}  // namespace services
