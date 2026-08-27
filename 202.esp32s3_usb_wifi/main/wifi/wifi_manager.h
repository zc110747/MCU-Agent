/*
 * wifi_manager.h
 *
 * Wi-Fi STA manager: scan, connect, auto-reconnect, status queries.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_CONNECT_FAILED
} wifi_state_t;

/**
 * @brief Initialise Wi-Fi (STA mode) and the event handlers.
 *        esp_netif_init() must have been called before this.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Trigger an active scan and fill ap_records (up to *count entries).
 *        *count is updated with the number actually returned.
 */
esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t *count);

/**
 * @brief Connect to the given AP (does not persist; use config_manager to save).
 */
esp_err_t wifi_manager_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect from the current AP.
 */
esp_err_t wifi_manager_disconnect(void);

wifi_state_t wifi_manager_get_state(void);
bool wifi_manager_is_connected(void);

esp_err_t wifi_manager_get_ip_info(esp_netif_ip_info_t *ip_info);
esp_err_t wifi_manager_get_dns(esp_netif_dns_info_t *dns);
int8_t    wifi_manager_get_rssi(void);
uint8_t   wifi_manager_get_channel(void);

/**
 * @brief Copy the currently-connected SSID into buf (may be empty if not connected).
 */
esp_err_t wifi_manager_get_ssid(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
