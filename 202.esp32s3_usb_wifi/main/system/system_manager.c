/*
 * system_manager.c
 *
 * Global status event group.
 */
#include "system_manager.h"

#include "esp_log.h"

static const char *TAG = "sys";
EventGroupHandle_t g_sys_evt = NULL;

esp_err_t system_manager_init(void)
{
    if (g_sys_evt == NULL) {
        g_sys_evt = xEventGroupCreate();
        if (g_sys_evt == NULL) {
            ESP_LOGE(TAG, "failed to create event group");
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "system manager initialised");
    return ESP_OK;
}

void system_manager_set_bits(EventBits_t bits)
{
    if (g_sys_evt != NULL) {
        xEventGroupSetBits(g_sys_evt, bits);
    }
}

void system_manager_clear_bits(EventBits_t bits)
{
    if (g_sys_evt != NULL) {
        xEventGroupClearBits(g_sys_evt, bits);
    }
}

EventBits_t system_manager_get_bits(void)
{
    return (g_sys_evt != NULL) ? xEventGroupGetBits(g_sys_evt) : 0;
}
