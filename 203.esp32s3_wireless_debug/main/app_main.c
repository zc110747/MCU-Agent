/**
 * @file app_main.c
 * @brief ESP32-S3 CMSIS-DAP Debug Probe - application entry.
 *
 * Boot flow:
 *   debug_init()      -> SWD pins to safe idle + debug mutex
 *   usb_device_init() -> USB PHY + TinyUSB + USB task (HID CMSIS-DAP)
 *   cmsis_dap_init()  -> DAP task consuming USB requests
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "usb_device.h"
#include "cmsis_dap.h"
#include "debug_engine.h"
#include "target.h"

static const char *TAG = "probe";

#if CONFIG_DEBUG_PROBE_SELFTEST
static void probe_selftest(void)
{
    target_info_t info;
    ESP_LOGI(TAG, "=== debug self-test ===");

    if (target_connect_and_identify(&info) != ESP_OK) {
        ESP_LOGW(TAG, "self-test: no target detected");
        return;
    }

    bool halted = false;
    if (debug_halt() == ESP_OK) {
        debug_is_halted(&halted);
        uint32_t r0 = 0;
        debug_read_register(0, &r0);
        uint32_t mem = 0;
        debug_read_word(0x20000000, &mem);
        ESP_LOGI(TAG, "self-test: halted=%d R0=0x%08" PRIx32 " [0x20000000]=0x%08" PRIx32,
                 halted, r0, mem);
        debug_run();
    }
    ESP_LOGI(TAG, "=== self-test done ===");
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 CMSIS-DAP Debug Probe V1");
    ESP_LOGI(TAG, "SWD: SWCLK=GPIO%d SWDIO=GPIO%d nRESET=GPIO%d, clock<=%d Hz",
             CONFIG_DEBUG_SWCLK_GPIO, CONFIG_DEBUG_SWDIO_GPIO,
             CONFIG_DEBUG_NRESET_GPIO, CONFIG_DEBUG_SWD_MAX_CLOCK_HZ);
    ESP_LOGI(TAG, "USB: VID=0x%04X PID=0x%04X \"%s\"",
             CONFIG_DEBUG_PROBE_USB_VID, CONFIG_DEBUG_PROBE_USB_PID,
             CONFIG_DEBUG_PROBE_USB_PRODUCT);

    ESP_ERROR_CHECK(debug_init());

    esp_err_t err = usb_device_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_device_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = cmsis_dap_init(usb_device_rx_queue());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cmsis_dap_init failed: %s", esp_err_to_name(err));
        return;
    }

#if CONFIG_DEBUG_PROBE_SELFTEST
    vTaskDelay(pdMS_TO_TICKS(2000));
    probe_selftest();
#endif
}
