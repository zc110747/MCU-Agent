/*
 * main.c
 *
 * ESP32-S3 USB Wi-Fi RNDIS adapter - application entry point.
 *
 * Startup sequence (phased):
 *   1. NVS / config
 *   2. esp_netif + default event loop
 *   3. system status event group
 *   4. USB RNDIS (Phase 2)  -> 192.168.4.1 + DHCP server
 *   5. Wi-Fi STA (auto-connect from NVS if configured)
 *   6. Web configuration server
 *
 * USB/RNDIS init is added in Phase 2; the call is intentionally left
 * commented until usb_rndis.c is integrated.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

#include "config_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "system_manager.h"
/* #include "usb_rndis.h" */   /* Phase 2 */

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 USB Wi-Fi RNDIS adapter ===");

    ESP_ERROR_CHECK(config_manager_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(system_manager_init());

    /* Phase 2: USB RNDIS provides the 192.168.4.1 interface + DHCP server. */
    /* ESP_ERROR_CHECK(usb_rndis_init()); */

    ESP_ERROR_CHECK(wifi_manager_init());

    /* Auto-connect using saved credentials, if present. */
    if (config_manager_is_configured()) {
        char ssid[CFG_SSID_MAX_LEN + 1] = {0};
        char password[CFG_PASSWORD_MAX_LEN + 1] = {0};
        if (config_manager_load_wifi(ssid, sizeof(ssid), password, sizeof(password)) == ESP_OK) {
            ESP_LOGI(TAG, "auto-connecting to saved SSID=%s", ssid);
            wifi_manager_connect(ssid, password);
        }
    }

    ESP_ERROR_CHECK(web_server_start());
    ESP_LOGI(TAG, "initialisation complete");

    /* Lightweight monitor loop (no busy-wait; status is event-driven). */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
