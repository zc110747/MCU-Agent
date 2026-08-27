/*
 * config_manager.c
 *
 * NVS-backed Wi-Fi configuration storage.
 * Wi-Fi credentials are persisted here and NEVER printed to logs.
 */
#include "config_manager.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "cfg";

static nvs_handle_t s_nvs_handle = 0;
static bool s_initialised = false;

esp_err_t config_manager_init(void)
{
    if (s_initialised) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated/needs erase, erasing...");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_flash_init failed");

    ESP_RETURN_ON_ERROR(nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle),
                        TAG, "nvs_open failed");

    s_initialised = true;
    ESP_LOGI(TAG, "config manager initialised");
    return ESP_OK;
}

bool config_manager_is_configured(void)
{
    if (!s_initialised) {
        return false;
    }
    uint8_t configured = 0;
    esp_err_t err = nvs_get_u8(s_nvs_handle, CFG_KEY_CONFIGURED, &configured);
    return (err == ESP_OK && configured == 1);
}

esp_err_t config_manager_load_wifi(char *ssid_buf, size_t ssid_len,
                                   char *pass_buf, size_t pass_len)
{
    if (!s_initialised || ssid_buf == NULL || pass_buf == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_get_str(s_nvs_handle, CFG_KEY_SSID, ssid_buf, &ssid_len);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(s_nvs_handle, CFG_KEY_PASSWORD, pass_buf, &pass_len);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t config_manager_save_wifi(const char *ssid, const char *password)
{
    if (!s_initialised || ssid == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (password == NULL) {
        password = "";
    }

    esp_err_t err;
    ESP_RETURN_ON_ERROR(nvs_set_str(s_nvs_handle, CFG_KEY_SSID, ssid), TAG, "set ssid");
    ESP_RETURN_ON_ERROR(nvs_set_str(s_nvs_handle, CFG_KEY_PASSWORD, password), TAG, "set password");
    ESP_RETURN_ON_ERROR(nvs_set_u8(s_nvs_handle, CFG_KEY_CONFIGURED, 1), TAG, "set configured");

    err = nvs_commit(s_nvs_handle);
    if (err == ESP_OK) {
        /* Password is intentionally NOT logged. */
        ESP_LOGI(TAG, "Wi-Fi config saved (ssid=%s)", ssid);
    }
    return err;
}

esp_err_t config_manager_factory_reset(void)
{
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(nvs_erase_key(s_nvs_handle, CFG_KEY_SSID), TAG, "erase ssid");
    ESP_RETURN_ON_ERROR(nvs_erase_key(s_nvs_handle, CFG_KEY_PASSWORD), TAG, "erase password");
    ESP_RETURN_ON_ERROR(nvs_erase_key(s_nvs_handle, CFG_KEY_CONFIGURED), TAG, "erase configured");
    ESP_RETURN_ON_ERROR(nvs_commit(s_nvs_handle), TAG, "commit");
    ESP_LOGI(TAG, "factory reset: all Wi-Fi configuration cleared");
    return ESP_OK;
}
