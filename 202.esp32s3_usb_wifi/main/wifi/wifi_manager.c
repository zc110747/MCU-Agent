/*
 * wifi_manager.c
 *
 * Wi-Fi STA manager: scan, connect, auto-reconnect, status queries.
 */
#include "wifi_manager.h"

#include <string.h>
#include "config_manager.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include "system_manager.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      5

static esp_netif_t *s_sta_netif = NULL;
static wifi_state_t s_state = WIFI_STATE_DISCONNECTED;
static int s_retry = 0;
static char s_connected_ssid[CFG_SSID_MAX_LEN] = {0};

/* Forward */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);

esp_err_t wifi_manager_init(void)
{
    if (s_sta_netif != NULL) {
        return ESP_OK; /* already initialised */
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &ip_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP,
                                                        &ip_event_handler, NULL, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE)); /* keep link responsive for NAT */

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        ESP_LOGE(TAG, "failed to create default STA netif");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    s_state = WIFI_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "Wi-Fi STA initialised");
    return ESP_OK;
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t *count)
{
    if (ap_records == NULL || count == NULL || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t max = *count;
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 300 } },
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan start failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        return err;
    }
    if (ap_count > max) {
        ap_count = max;
    }
    err = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    if (err == ESP_OK) {
        *count = ap_count;
        ESP_LOGI(TAG, "scan complete: %d AP(s) found", ap_count);
    }
    return err;
}

esp_err_t wifi_manager_connect(const char *ssid, const char *password)
{
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_retry = 0;
    s_state = WIFI_STATE_CONNECTING;

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password && strlen(password) > 0) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "connecting to SSID=%s", ssid);
    return esp_wifi_connect();
}

esp_err_t wifi_manager_disconnect(void)
{
    s_state = WIFI_STATE_DISCONNECTED;
    return esp_wifi_disconnect();
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_state;
}

bool wifi_manager_is_connected(void)
{
    return s_state == WIFI_STATE_CONNECTED;
}

esp_err_t wifi_manager_get_ip_info(esp_netif_ip_info_t *ip_info)
{
    if (s_sta_netif == NULL || ip_info == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_netif_get_ip_info(s_sta_netif, ip_info);
}

esp_err_t wifi_manager_get_dns(esp_netif_dns_info_t *dns)
{
    if (s_sta_netif == NULL || dns == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, dns);
}

int8_t wifi_manager_get_rssi(void)
{
    int8_t rssi = 0;
    if (s_state == WIFI_STATE_CONNECTED) {
        esp_wifi_sta_get_rssi(&rssi);
    }
    return rssi;
}

uint8_t wifi_manager_get_channel(void)
{
    uint8_t primary = 0;
    if (s_state == WIFI_STATE_CONNECTED) {
        esp_wifi_get_channel(&primary);
    }
    return primary;
}

esp_err_t wifi_manager_get_ssid(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_state != WIFI_STATE_CONNECTED) {
        buf[0] = '\0';
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(buf, s_connected_ssid, len - 1);
    buf[len - 1] = '\0';
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base != WIFI_EVENT) {
        return;
    }
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi STA started");
            break;

        case WIFI_EVENT_STA_CONNECTED:
            s_retry = 0;
            s_state = WIFI_STATE_CONNECTED;
            system_manager_set_bits(SYS_EVT_WIFI_CONNECTED);
            {
                wifi_event_sta_connected_t *ev = (wifi_event_sta_connected_t *)event_data;
                strncpy(s_connected_ssid, (const char *)ev->ssid, sizeof(s_connected_ssid) - 1);
            }
            ESP_LOGI(TAG, "Wi-Fi connected to SSID=%s", s_connected_ssid);
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            s_state = WIFI_STATE_DISCONNECTED;
            system_manager_clear_bits(SYS_EVT_WIFI_CONNECTED | SYS_EVT_NET_READY);
            {
                wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Wi-Fi disconnected (reason=%d)", ev->reason);
            }
            /* Auto-reconnect: Wi-Fi stack retries automatically when started;
               trigger a fresh connect attempt up to WIFI_MAX_RETRY. */
            if (s_retry < WIFI_MAX_RETRY) {
                s_retry++;
                ESP_LOGI(TAG, "auto-reconnect attempt %d/%d", s_retry, WIFI_MAX_RETRY);
                esp_wifi_connect();
            } else {
                s_state = WIFI_STATE_CONNECT_FAILED;
                ESP_LOGE(TAG, "Wi-Fi reconnect gave up after %d attempts", WIFI_MAX_RETRY);
            }
            break;

        default:
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_base != IP_EVENT) {
        return;
    }
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        s_state = WIFI_STATE_CONNECTED;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "gateway: " IPSTR, IP2STR(&ev->ip_info.gw));
        ESP_LOGI(TAG, "netmask: " IPSTR, IP2STR(&ev->ip_info.netmask));
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "lost IP (DHCP lease expired or AP unreachable)");
    }
}
