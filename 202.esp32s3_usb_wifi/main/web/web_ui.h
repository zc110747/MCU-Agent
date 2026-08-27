/*
 * web_ui.h
 *
 * HTML/JSON builders for the configuration web interface.
 * The HTML page itself is embedded as a binary resource (see CMakeLists.txt,
 * EMBED_FILES "web/index.html") and referenced via index_html_start/_end.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a JSON status document describing USB/RNDIS/Wi-Fi state.
 *        Caller must free() the returned string.
 */
char *web_ui_build_status_json(void);

/**
 * @brief Build a JSON document listing scanned APs.
 *        Caller must free() the returned string.
 */
char *web_ui_build_scan_json(const wifi_ap_record_t *aps, uint16_t count);

/**
 * @brief Build a simple { "result": "..." } JSON. Caller frees.
 */
char *web_ui_build_result_json(const char *result);

#ifdef __cplusplus
}
#endif
