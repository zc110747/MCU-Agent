/**
 * @file main.cpp
 * @brief Ebook LVGL -- application entry point.
 *
 * Boot order (hardware first, UI last):
 *   Phase 0 : NVS, system information, banner
 *   Phase 1 : CH422G expander -> LCD reset -> RGB panel -> LVGL -> backlight
 *   Phase 2+: Home page and the rest of the application
 */

#include <stdio.h>

#include "board_config.h"
#include "display_driver.h"
#include "io_expander.h"
#include "lvgl_port.h"
#include "system_info.h"
#include "app_manager.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static void log_banner(void)
{
    system_info_t info;
    system_info_collect(&info);

    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, " Ebook LVGL");
    ESP_LOGI(TAG, " %s", BOARD_NAME);
    ESP_LOGI(TAG, " display : %dx%d landscape, RGB565", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    ESP_LOGI(TAG, "================================================");
    ESP_LOGI(TAG, "ESP32-S3   : OK");
    ESP_LOGI(TAG, "Flash      : %s", info.flash_size_bytes ? "OK" : "FAIL");
    ESP_LOGI(TAG, "PSRAM      : %s", info.psram_size_bytes ? "OK" : "FAIL");
}

extern "C" void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    log_banner();

    /* ---- platform bring-up ------------------------------------------- */
    ESP_ERROR_CHECK(io_expander_init());
    ESP_LOGI(TAG, "CH422G     : OK");

    err = display_init();
    ESP_LOGI(TAG, "LCD        : %s", err == ESP_OK ? "OK" : "FAIL");
    ESP_ERROR_CHECK(err);

    err = lvgl_port_start();
    ESP_LOGI(TAG, "LVGL       : %s", err == ESP_OK ? "OK" : "FAIL");
    ESP_ERROR_CHECK(err);

    /* Backlight last: the panel now shows a defined (black) frame, so powering
     * the LED string here cannot flash a white screen.                    */
    if (!lvgl_port_acquire(0)) {
        ESP_LOGE(TAG, "LVGL lock not available");
    }
    ESP_ERROR_CHECK(app_manager_init());
    lvgl_port_release();
    ESP_ERROR_CHECK(display_backlight_set(true));

    system_info_log_memory(TAG);

    /* ---- application task --------------------------------------------- */
    app_manager_run();

    /* app_manager_run() only returns if the application asked for a restart. */
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
