/**
 * @file net_service.h
 * @brief Network status for the Settings page.
 *
 * SCOPE, STATED PLAINLY
 * ---------------------
 * This build does NOT bring up WiFi.  There are no credentials on the device,
 * and enabling the WiFi stack costs internal SRAM that the RGB panel's DMA
 * descriptors compete for - so it is a change that has to be made with a
 * measurement in hand, not folded into the same commit as nine new pages.
 *
 * What this service therefore does is tell the truth: the state is reported as
 * it actually is, and the Settings page renders that state.  When the WiFi
 * bring-up lands it replaces the bodies of net_init() / net_scan_start() and
 * nothing above this header changes - the page never knew how the answer was
 * produced.
 *
 * The enums already carry the states that a real implementation will need, so
 * the page's error handling is written once and stays correct.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace services {

enum class WifiState : uint8_t {
    Off,            /* the stack is not running                  */
    NotConfigured,  /* running, but no credentials stored        */
    Idle,           /* configured, not associated                */
    Connected,      /* associated, holds an IP                   */
    Error,          /* initialisation or association failed      */
    Unsupported,    /* the build has no WiFi support at all      */
};

constexpr size_t kWifiMaxAps = 16;

struct WifiAp {
    char   ssid[33];
    int8_t rssi;
    bool   secure;
};

/** @brief Start the network service. Never fatal to the application. */
esp_err_t net_init();

/** @brief Current WiFi state. */
WifiState net_wifi_state();

/** @brief Short human readable form of @p st. */
const char *net_wifi_state_text(WifiState st);

/** @brief SSID the device is associated with, or an empty string. */
const char *net_ssid();

/** @brief IPv4 address as a string, or "0.0.0.0". */
const char *net_ip();

/* ---- scanning ----------------------------------------------------------- */

/** @brief Ask for a scan. Returns immediately. */
esp_err_t net_scan_start();

/** @brief True while a scan is in progress. */
bool net_scan_busy();

/** @brief Results of the last scan; @p count receives the number of entries. */
const WifiAp *net_scan_results(size_t *count);

/** @brief Re-read the scan results into @p out (up to @p max). */
size_t net_scan_copy(WifiAp *out, size_t max);

/** @brief Signal strength as 0..4 bars, for a cheap text indicator. */
int net_rssi_bars(int8_t rssi);

}  // namespace services
