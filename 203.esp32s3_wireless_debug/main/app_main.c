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
#include "wifi_dap.h"
#include "debug_engine.h"
#include "target.h"

static const char *TAG = "probe";

/* ------------------------------------------------------------------------- */
/* Transport arbitration: USB first, Wi-Fi only as a fallback                */
/* ------------------------------------------------------------------------- */
#if CONFIG_DEBUG_ENABLE_USB && CONFIG_DEBUG_ENABLE_WIFI
/**
 * Wait for a USB host to attach/enumerate.
 *
 * Why this exists: once esp_wifi starts the RF on ESP32-S3, the USB OTG HID
 * transfers become unreliable (host sees CMSIS-DAP command timeouts). The
 * SoftAP must therefore only be started when there is NO USB host, i.e. the
 * probe is powered from a plain 5V supply / power bank and used wirelessly.
 */
static bool wait_for_usb_host(uint32_t timeout_ms)
{
    const uint32_t poll_ms = 100;
    for (uint32_t waited = 0; waited < timeout_ms; waited += poll_ms) {
        if (usb_device_is_connected()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_ms));
    }
    return usb_device_is_connected();
}
#endif

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
    ESP_LOGI(TAG, "JTAG: TCK=GPIO%d TMS=GPIO%d TDI=GPIO%d TDO=GPIO%d nTRST=GPIO%d, clock<=%d Hz",
             CONFIG_DEBUG_SWCLK_GPIO, CONFIG_DEBUG_SWDIO_GPIO,
             CONFIG_DEBUG_TDI_GPIO, CONFIG_DEBUG_TDO_GPIO,
             CONFIG_DEBUG_NTRST_GPIO, CONFIG_DEBUG_JTAG_MAX_CLOCK_HZ);
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

    /* Wireless CMSIS-DAP transport (AP + TCP 50000).
     *
     * USB-FIRST POLICY: the SoftAP is a FALLBACK, not a parallel transport.
     * Wi-Fi RF on ESP32-S3 corrupts the USB OTG HID timing, so if a USB host
     * is present we never touch esp_wifi at all. Reuses cmsis_dap_execute()
     * under the shared debug_engine lock, so the SWD/JTAG core is unchanged. */
#if CONFIG_DEBUG_ENABLE_WIFI
    bool usb_host_present = false;
#if CONFIG_DEBUG_ENABLE_USB
    usb_host_present = wait_for_usb_host((uint32_t)CONFIG_WIFI_DAP_USB_WAIT_MS);
#endif
    if (usb_host_present) {
        /* WARN: project log level is WARN, INFO would not be visible. */
        ESP_LOGW(TAG, "USB host enumerated (vbus=%d mounted=%d) -> USB mode, Wi-Fi AP NOT started",
                 (int)usb_device_vbus_present(), (int)usb_device_is_connected());
    } else {
        ESP_LOGW(TAG, "no USB host in %d ms (vbus=%d mounted=%d) -> starting Wi-Fi AP mode",
                 (int)CONFIG_WIFI_DAP_USB_WAIT_MS,
                 (int)usb_device_vbus_present(), (int)usb_device_is_connected());
        err = wifi_dap_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi_dap_init failed: %s (USB transport still active)",
                     esp_err_to_name(err));
        }
    }
#endif

#if CONFIG_DEBUG_PROBE_SELFTEST
    vTaskDelay(pdMS_TO_TICKS(2000));
    probe_selftest();
#endif
}
