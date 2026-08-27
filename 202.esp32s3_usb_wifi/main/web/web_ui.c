/*
 * web_ui.c
 *
 * JSON builders for the configuration web interface.
 */
#include "web_ui.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "config_manager.h"
#include "wifi_manager.h"
#include "system_manager.h"

static const char *TAG = "web_ui";

/* Static IP assigned to the USB (RNDIS) interface. */
#define USB_NETIF_IP_STR "192.168.4.1"

static void json_escape(char *dst, size_t dst_cap, const char *src)
{
    size_t j = 0;
    if (dst == NULL || dst_cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_cap; i++) {
        char c = src[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= dst_cap) break;
            dst[j++] = '\\';
            dst[j++] = c;
        } else if (c == '\n') {
            if (j + 2 >= dst_cap) break;
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

char *web_ui_build_status_json(void)
{
    wifi_state_t st = wifi_manager_get_state();
    const char *wifi_status = (st == WIFI_STATE_CONNECTED) ? "Connected"
                             : (st == WIFI_STATE_CONNECTING) ? "Connecting"
                             : (st == WIFI_STATE_CONNECT_FAILED) ? "Failed"
                             : "Disconnected";

    char ssid[CFG_SSID_MAX_LEN + 1] = {0};
    char wifi_ip[16] = "0.0.0.0";
    char gateway[16] = "0.0.0.0";
    char dns[16] = "0.0.0.0";

    if (st == WIFI_STATE_CONNECTED) {
        esp_netif_ip_info_t ip_info;
        esp_netif_dns_info_t dns_info;
        if (wifi_manager_get_ip_info(&ip_info) == ESP_OK) {
            snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&ip_info.ip));
            snprintf(gateway, sizeof(gateway), IPSTR, IP2STR(&ip_info.gw));
        }
        if (wifi_manager_get_dns(&dns_info) == ESP_OK) {
            snprintf(dns, sizeof(dns), IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        }
        wifi_manager_get_ssid(ssid, sizeof(ssid));
    }

    EventBits_t bits = system_manager_get_bits();
    const char *usb = (bits & SYS_EVT_USB_CONNECTED) ? "Connected" : "Disconnected";
    const char *rndis = (bits & SYS_EVT_RNDIS_UP) ? "Up" : "Down";
    const char *internet = (bits & SYS_EVT_NET_READY) ? "OK" : "No";

    int8_t rssi = wifi_manager_get_rssi();
    uint8_t channel = wifi_manager_get_channel();

    char buf[640];
    int n = snprintf(buf, sizeof(buf),
        "{\"usb\":\"%s\",\"rndis\":\"%s\",\"wifi_status\":\"%s\",\"ssid\":\"%s\","
        "\"rssi\":%d,\"channel\":%u,\"wifi_ip\":\"%s\",\"gateway\":\"%s\","
        "\"dns\":\"%s\",\"usb_ip\":\"%s\",\"internet\":\"%s\"}",
        usb, rndis, wifi_status, ssid, rssi, channel,
        wifi_ip, gateway, dns, USB_NETIF_IP_STR, internet);

    if (n < 0) return NULL;
    char *out = malloc((size_t)n + 1);
    if (out) {
        snprintf(out, (size_t)n + 1, "%s", buf);
    }
    return out;
}

char *web_ui_build_scan_json(const wifi_ap_record_t *aps, uint16_t count)
{
    size_t cap = (size_t)count * 256 + 64;
    char *buf = malloc(cap);
    if (buf == NULL) {
        return NULL;
    }
    int off = snprintf(buf, cap, "{\"count\":%u,\"aps\":[", count);
    for (uint16_t i = 0; i < count; i++) {
        const char *sec = "OPEN";
        switch (aps[i].authmode) {
            case WIFI_AUTH_WEP: sec = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: sec = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: sec = "WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: sec = "WPA3"; break;
            case WIFI_AUTH_WPA2_WPA3_PSK: sec = "WPA2/3"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: sec = "WPA/WPA2"; break;
            default: break;
        }
        char essid[CFG_SSID_MAX_LEN + 1];
        memcpy(essid, aps[i].ssid, CFG_SSID_MAX_LEN);
        essid[CFG_SSID_MAX_LEN] = '\0';

        char esc_ssid[CFG_SSID_MAX_LEN * 2 + 8];
        json_escape(esc_ssid, sizeof(esc_ssid), essid);

        off += snprintf(buf + off, cap - (size_t)off,
                        "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%u,\"auth\":\"%s\"}",
                        (i ? "," : ""), esc_ssid, aps[i].rssi, aps[i].primary, sec);
    }
    off += snprintf(buf + off, cap - (size_t)off, "]}");
    if (off < 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

char *web_ui_build_result_json(const char *result)
{
    size_t cap = strlen(result) + 32;
    char *buf = malloc(cap);
    if (buf == NULL) {
        return NULL;
    }
    char esc[256];
    json_escape(esc, sizeof(esc), result);
    snprintf(buf, cap, "{\"result\":\"%s\"}", esc);
    return buf;
}
