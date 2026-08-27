/*
 * web_server.h
 *
 * Lightweight configuration web server (esp_http_server).
 * Binds on all interfaces so it is reachable from the USB/RNDIS subnet
 * (192.168.4.1) and, once Wi-Fi is up, from the STA side.
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Start the HTTP configuration server. Idempotent.
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the HTTP configuration server.
 */
void web_server_stop(void);
