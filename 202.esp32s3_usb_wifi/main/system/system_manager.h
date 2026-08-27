/*
 * system_manager.h
 *
 * Global system status event group shared across modules (USB, RNDIS, Wi-Fi, Net).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* Global status bits */
extern EventGroupHandle_t g_sys_evt;

#define SYS_EVT_USB_CONNECTED  (1 << 0)  /* USB cable plugged, TinyUSB enumerated */
#define SYS_EVT_RNDIS_UP       (1 << 1)  /* RNDIS link is up */
#define SYS_EVT_WIFI_CONNECTED (1 << 2)  /* Wi-Fi STA associated + got IP */
#define SYS_EVT_NET_READY      (1 << 3)  /* NAT/DNS up, Internet reachable via Wi-Fi */

esp_err_t system_manager_init(void);

void system_manager_set_bits(EventBits_t bits);
void system_manager_clear_bits(EventBits_t bits);
EventBits_t system_manager_get_bits(void);
