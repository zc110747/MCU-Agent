/**
 * @file wifi_dap.c
 * @brief CMSIS-DAP over WiFi (TCP) transport — minimal Phase-1 extension.
 *
 * Design constraints (from project spec):
 *   - MUST NOT modify: swd.c, jtag.c, dap.c, the CMSIS-DAP command handler,
 *     or the USB HID device layer.
 *   - MUST reuse the existing DAP core via cmsis_dap_execute() and serialize
 *     access with debug_engine_lock()/debug_engine_unlock().
 *
 * This file implements only the NEW wireless transport:
 *   1. A SoftAP (ESP32-DAP-XXXX @ 192.168.4.1) so a PC can connect with no
 *      external router.
 *   2. A TCP server on port WIFI_DAP_TCP_PORT that receives length-prefixed
 *      CMSIS-DAP packets, runs them through the shared core, and sends back
 *      length-prefixed responses.
 *
 * Frame format (see wifi_dap.h for the header layout):
 *   [ cmsis_dap_tcp_packet_hdr_t ][ CMSIS-DAP payload (variable, <=64 B) ]
 */
#include <string.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "nvs_flash.h"

#include "wifi_dap.h"
#include "cmsis_dap.h"
#include "debug_engine.h"

static const char *TAG = "wifi_dap";

/* ------------------------------------------------------------------------- */
/* Tunables                                                                  */
/* ------------------------------------------------------------------------- */
#define WIFI_DAP_AP_SSID_PREFIX "ESP32-DAP-"
#define WIFI_DAP_AP_IP          "192.168.4.1"
#define WIFI_DAP_AP_GW          "192.168.4.1"
#define WIFI_DAP_AP_NETMASK     "255.255.255.0"

#define WIFI_DAP_TASK_STACK     (6 * 1024)
#define WIFI_DAP_TASK_PRIO      5

/* Re-assembly buffer: header + one max payload, plus slack for stream residue. */
#define WIFI_DAP_RXBUF_SIZE     (CMSIS_DAP_TCP_HDR_SIZE + CMSIS_DAP_TCP_MAX_PAYLOAD + 16)

/* ------------------------------------------------------------------------- */
/* Forward declarations                                                     */
/* ------------------------------------------------------------------------- */
static esp_err_t wifi_dap_start_ap(void);
static void wifi_dap_tcp_task(void *arg);

/* One active client at a time (matches the resource-owner model in spec). */
static bool s_client_active = false;

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */
esp_err_t wifi_dap_init(void)
{
#if !CONFIG_DEBUG_ENABLE_WIFI
    ESP_LOGW(TAG, "Wi-Fi transport disabled (CONFIG_DEBUG_ENABLE_WIFI=n)");
    return ESP_ERR_NOT_SUPPORTED;
#else
    /* NVS is required by esp_wifi; only init if not already done. Guard with a
     * local flag so we don't clobber an existing NVS partition owned by USB. */
    static bool s_nvs_done = false;
    if (!s_nvs_done) {
        esp_err_t nvs_ret = nvs_flash_init();
        if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "nvs_flash_init needs erase, wiping NVS");
            ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
            nvs_ret = nvs_flash_init();
        }
        ESP_RETURN_ON_ERROR(nvs_ret, TAG, "nvs_flash_init");
        s_nvs_done = true;
    }

    /* Netif must be initialized before creating the AP interface. */
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");

    ESP_RETURN_ON_ERROR(wifi_dap_start_ap(), TAG, "start AP");

    BaseType_t t = xTaskCreate(wifi_dap_tcp_task, "wifi_dap_tcp",
                               WIFI_DAP_TASK_STACK, NULL,
                               WIFI_DAP_TASK_PRIO, NULL);
    if (t != pdPASS) {
        ESP_LOGE(TAG, "failed to create tcp task");
        return ESP_FAIL;
    }

    /* WARN level on purpose: the project ships CONFIG_LOG_DEFAULT_LEVEL=WARN
     * (UART logging must never become the HID download bottleneck), so an
     * INFO line here would be invisible during bring-up. Start-up only, so
     * it does not affect the DAP hot path. */
    ESP_LOGW(TAG, "wireless CMSIS-DAP transport ready: connect to AP, target TCP %s:%d",
             WIFI_DAP_AP_IP, WIFI_DAP_TCP_PORT);
    return ESP_OK;
#endif
}

/* ------------------------------------------------------------------------- */
/* SoftAP                                                                    */
/* ------------------------------------------------------------------------- */
static esp_err_t wifi_dap_start_ap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init");

    esp_netif_create_default_wifi_ap();

    /* Build SSID ESP32-DAP-XXXX from the STA MAC (last 2 bytes, uppercase). */
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "esp_read_mac");
    char ssid[sizeof(WIFI_DAP_AP_SSID_PREFIX) + 5];
    snprintf(ssid, sizeof(ssid), "%s%02X%02X", WIFI_DAP_AP_SSID_PREFIX,
             mac[4], mac[5]);

    wifi_config_t ap_config = {
        .ap = {
            .ssid = "",
            .password = "",
            .ssid_len = (uint8_t)strlen(ssid),
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN,   /* open AP; add password in Phase 2+ */
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };
    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid) - 1);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set_mode AP");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start");

    /* Static IP for the AP interface. */
    esp_netif_ip_info_t ip_info;
    inet_pton(AF_INET, WIFI_DAP_AP_IP, &ip_info.ip);
    inet_pton(AF_INET, WIFI_DAP_AP_GW, &ip_info.gw);
    inet_pton(AF_INET, WIFI_DAP_AP_NETMASK, &ip_info.netmask);
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif) {
        ESP_RETURN_ON_ERROR(esp_netif_dhcps_stop(ap_netif), TAG, "dhcps stop");
        ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(ap_netif, &ip_info), TAG, "set ip");
        ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(ap_netif), TAG, "dhcps start");
    } else {
        ESP_LOGW(TAG, "AP netif handle not found; using default IP");
    }

    ESP_LOGW(TAG, "SoftAP '%s' up @ %s", ssid, WIFI_DAP_AP_IP);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* TCP server + DAP dispatch                                                */
/* ------------------------------------------------------------------------- */

/* Append received bytes into the reassembly buffer; returns bytes added. */
static int rxbuf_fill(uint8_t *buf, size_t *len, size_t cap, int sock)
{
    size_t space = cap - *len;
    if (space == 0) {
        return -1; /* full */
    }
    ssize_t n = recv(sock, buf + *len, space, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    if (n == 0) {
        return -2; /* peer closed */
    }
    *len += (size_t)n;
    return (int)n;
}

/* Try to parse one complete framed packet from buf.
 * Returns 0 and sets *payload and *payload_len on success,
 *         -EAGAIN if more bytes are needed,
 *         negative on a hard error. */
static int rxbuf_parse(const uint8_t *buf, size_t len,
                       const uint8_t **payload, size_t *payload_len,
                       size_t *consumed)
{
    if (len < CMSIS_DAP_TCP_HDR_SIZE) {
        return -EAGAIN;
    }
    cmsis_dap_tcp_packet_hdr_t hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    if (hdr.signature != CMSIS_DAP_TCP_HDR_SIGNATURE) {
        ESP_LOGE(TAG, "bad signature 0x%08" PRIx32, (uint32_t)hdr.signature);
        return -EINVAL;
    }
    if (hdr.packet_type != CMSIS_DAP_TCP_PKT_REQUEST) {
        ESP_LOGE(TAG, "unexpected packet_type 0x%02X", hdr.packet_type);
        return -EINVAL;
    }
    size_t need = CMSIS_DAP_TCP_HDR_SIZE + hdr.length;
    if (len < need) {
        return -EAGAIN;
    }
    if (hdr.length > CMSIS_DAP_TCP_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "payload %u exceeds max %u", hdr.length, CMSIS_DAP_TCP_MAX_PAYLOAD);
        return -EMSGSIZE;
    }
    *payload = buf + CMSIS_DAP_TCP_HDR_SIZE;
    *payload_len = hdr.length;
    *consumed = need;
    return 0;
}

static void send_framed(int sock, const uint8_t *response, size_t resp_len)
{
    uint8_t out[CMSIS_DAP_TCP_HDR_SIZE + CMSIS_DAP_TCP_MAX_PAYLOAD];
    cmsis_dap_tcp_packet_hdr_t *h = (cmsis_dap_tcp_packet_hdr_t *)out;
    h->signature = CMSIS_DAP_TCP_HDR_SIGNATURE;
    h->length = (uint16_t)resp_len;
    h->packet_type = CMSIS_DAP_TCP_PKT_RESPONSE;
    h->reserved = 0;
    if (resp_len > 0) {
        memcpy(out + CMSIS_DAP_TCP_HDR_SIZE, response, resp_len);
    }
    size_t total = CMSIS_DAP_TCP_HDR_SIZE + resp_len;
    size_t sent = 0;
    while (sent < total) {
        ssize_t n = send(sock, out + sent, total - sent, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ESP_LOGE(TAG, "send failed: %s", strerror(errno));
            return;
        }
        sent += (size_t)n;
    }
}

static void wifi_dap_tcp_task(void *arg)
{
    (void)arg;

    int listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0) {
        ESP_LOGE(TAG, "socket(): %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(WIFI_DAP_TCP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "bind(): %s", strerror(errno));
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }
    if (listen(listen_fd, 1) != 0) {
        ESP_LOGE(TAG, "listen(): %s", strerror(errno));
        close(listen_fd);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "TCP server listening on port %d", WIFI_DAP_TCP_PORT);

    for (;;) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0) {
            ESP_LOGE(TAG, "accept(): %s", strerror(errno));
            continue;
        }
        /* Only one active client at a time (resource-owner model, Phase 4). */
        if (s_client_active) {
            ESP_LOGW(TAG, "dropping extra connection (busy)");
            close(client_fd);
            continue;
        }
        s_client_active = true;
        ESP_LOGI(TAG, "client connected");

        uint8_t rxbuf[WIFI_DAP_RXBUF_SIZE];
        size_t rxlen = 0;

        for (;;) {
            int r = rxbuf_fill(rxbuf, &rxlen, sizeof(rxbuf), client_fd);
            if (r < 0) {
                ESP_LOGI(TAG, "client disconnected");
                break;
            }

            const uint8_t *payload;
            size_t payload_len, consumed;
            int pr = rxbuf_parse(rxbuf, rxlen, &payload, &payload_len, &consumed);
            while (pr == 0) {
                /* Dispatch through the shared DAP core, serialized by the lock. */
                uint8_t response[CMSIS_DAP_TCP_MAX_PAYLOAD];
                debug_engine_lock(portMAX_DELAY);
                uint32_t ret = cmsis_dap_execute(payload, (uint32_t)payload_len, response);
                debug_engine_unlock();

                size_t resp_len = (size_t)(ret & 0xFFFFU);
                if (resp_len > CMSIS_DAP_TCP_MAX_PAYLOAD) {
                    resp_len = CMSIS_DAP_TCP_MAX_PAYLOAD;
                }
                send_framed(client_fd, response, resp_len);

                /* Consume the processed frame, keep any residue. */
                memmove(rxbuf, rxbuf + consumed, rxlen - consumed);
                rxlen -= consumed;
                pr = rxbuf_parse(rxbuf, rxlen, &payload, &payload_len, &consumed);
            }
            if (pr != -EAGAIN) {
                /* Hard framing error: tear down the connection. */
                ESP_LOGE(TAG, "frame error %d, closing", pr);
                break;
            }
        }

        close(client_fd);
        s_client_active = false;
        ESP_LOGI(TAG, "client session ended");
    }
}
