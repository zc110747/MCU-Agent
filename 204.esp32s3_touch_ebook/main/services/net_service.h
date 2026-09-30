/**
 * @file net_service.h
 * @brief WiFi configuration and connection, for the Settings page.
 *
 * WHAT THIS OWNS
 * --------------
 * Credential storage (NVS), the station interface, scanning, and the state the
 * Settings page renders.  It does not own the panel, the UI or the retry
 * policy's presentation - it reports, the page draws.
 *
 * ONE THING TO KNOW BEFORE CHANGING IT
 * ------------------------------------
 * Enabling the WiFi stack costs **internal** SRAM, and this board's RGB panel
 * is driven by DMA whose descriptors and bounce buffers come from that same
 * pool.  That combination has already produced a visible fault once - an image
 * displaced by a constant number of pixels, from DMA bandwidth starvation.  So
 * any change here is verified in two steps, with a look at the glass in
 * between: bring the stack up and check the display, *then* connect and check
 * it again.  A green build log says nothing about this.
 *
 * `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` keeps the traffic buffers in PSRAM
 * and is what makes this affordable at all; do not turn it off.
 *
 * THREADING
 * ---------
 * State is written by the esp_event task and read by the LVGL task.  The
 * three values a caller reads together (state, SSID, IP) live in a small
 * snapshot that is double-buffered and published with an atomic index, so a
 * reader can never see an SSID from one connection with an IP from another -
 * and never a half-written string.  net_generation() exists so a caller can
 * skip redrawing when nothing has changed.
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
    Connecting,     /* associating                               */
    Connected,      /* associated, holds an IP                   */
    Error,          /* initialisation or association failed      */
    Unsupported,    /* the build has no WiFi support at all      */
};

constexpr size_t kWifiMaxAps   = 16;
constexpr size_t kWifiSsidMax  = 33;   /* 32 + NUL, the 802.11 limit */
constexpr size_t kWifiPassMax  = 65;   /* 64 + NUL */

struct WifiAp {
    char   ssid[kWifiSsidMax];
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

/**
 * @brief RSSI of the current link in dBm, or 0 when not associated.
 *
 * 0 is outside the range a real measurement can take, so it doubles as
 * "unknown" without a second flag.  Read together with net_ssid() and
 * net_ip(); see the threading note at the top for why they cannot disagree.
 */
int net_rssi();

/**
 * @brief Bumped whenever state, SSID or IP changes.
 *
 * Callers that poll should compare this rather than the strings, and redraw
 * only on a change - the value label would otherwise be rewritten (and the
 * widget invalidated) on every tick for no reason.
 */
uint32_t net_generation();

/* ---- credentials and association ---------------------------------------- */

/** @brief True when an SSID is stored, whether or not it is connected. */
bool net_wifi_has_credentials();

/**
 * @brief Store credentials and associate.
 *
 * Credentials go to NVS *first*, so a success here means the next boot has
 * them too.  Returns as soon as the stack has been told; the outcome arrives
 * later as a state change with a reason in the log.
 *
 * @param ssid      must be non-empty and fit kWifiSsidMax
 * @param password  may be empty or nullptr for an open network
 */
esp_err_t net_wifi_connect(const char *ssid, const char *password);

/**
 * @brief Associate again using the credentials already stored.
 *
 * What the "Connect" button does: the usual case is that the network is
 * configured and the link dropped, or the device booted before the access
 * point was up.  Returns ESP_ERR_NOT_FOUND when nothing is stored, which the
 * page reports as "configure one first" rather than as a failure.
 */
esp_err_t net_wifi_reconnect();

/** @brief Drop the association but keep the stored credentials. */
esp_err_t net_wifi_disconnect();

/** @brief Drop the association and erase the stored credentials. */
esp_err_t net_wifi_forget();

/* ---- scanning ----------------------------------------------------------- */

/** @brief Ask for a scan. Returns immediately. */
esp_err_t net_scan_start();

/** @brief True while a scan is in progress. */
bool net_scan_busy();

/** @brief Results of the last scan, strongest first; @p count gets the number. */
const WifiAp *net_scan_results(size_t *count);

/** @brief Re-read the scan results into @p out (up to @p max). */
size_t net_scan_copy(WifiAp *out, size_t max);

/** @brief Signal strength as 0..4 bars, for a cheap text indicator. */
int net_rssi_bars(int8_t rssi);

}  // namespace services
