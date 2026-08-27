/*
 * config_manager.h
 *
 * NVS-backed Wi-Fi configuration storage.
 * Wi-Fi credentials are persisted here and NEVER printed to logs.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NVS namespace and keys */
#define CFG_NVS_NAMESPACE      "rndis_wifi"
#define CFG_KEY_SSID           "ssid"
#define CFG_KEY_PASSWORD       "password"
#define CFG_KEY_CONFIGURED     "configured"

/* Maximum lengths (must match ESP32 Wi-Fi limits) */
#define CFG_SSID_MAX_LEN       32
#define CFG_PASSWORD_MAX_LEN   64

/**
 * @brief Initialise NVS and open the configuration namespace.
 */
esp_err_t config_manager_init(void);

/**
 * @brief Return true if a Wi-Fi configuration has been saved previously.
 */
bool config_manager_is_configured(void);

/**
 * @brief Load saved Wi-Fi credentials into the provided buffers.
 *
 * @param ssid_buf      output buffer, CFG_SSID_MAX_LEN bytes
 * @param ssid_len      size of ssid_buf
 * @param pass_buf      output buffer, CFG_PASSWORD_MAX_LEN bytes
 * @param pass_len      size of pass_buf
 * @return ESP_OK if credentials were loaded, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t config_manager_load_wifi(char *ssid_buf, size_t ssid_len,
                                   char *pass_buf, size_t pass_len);

/**
 * @brief Persist Wi-Fi credentials.
 */
esp_err_t config_manager_save_wifi(const char *ssid, const char *password);

/**
 * @brief Clear all saved configuration (factory reset).
 */
esp_err_t config_manager_factory_reset(void);

#ifdef __cplusplus
}
#endif
