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
#include "clock_service.h"
#include "display_driver.h"
#include "io_expander.h"
#include "lvgl_port.h"
#include "net_service.h"
#include "sd_font.h"
#include "storage_service.h"
#include "system_info.h"
#include "touch_driver.h"
#include "weather_service.h"
#include "app_manager.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

/* ---------------------------------------------------------------------------
 * FreeRTOS task layout (the whole system has exactly three application-level
 * tasks; everything else is ESP-IDF's own):
 *
 *   task        core  prio  stack  created by         owns
 *   ----------  ----  ----  -----  -----------------  --------------------------
 *   main        0     1     8192   FreeRTOS/IDF       boot sequence, then exits
 *   taskLVGL     1    6     8192   esp_lvgl_port      ALL UI: input polling,
 *                                                     animation, rendering
 *   app          0    4     4096   this file          application lifetime,
 *                                                     health + memory heartbeat
 *
 * Reasoning: the UI must never be queued behind anything else, so taskLVGL has
 * the highest application priority and its own core.  The app task is lower
 * and separate so its 30 s heartbeat cannot perturb a flush.
 * --------------------------------------------------------------------------- */
static constexpr uint32_t kAppTaskStack    = 4096;
static constexpr UBaseType_t kAppTaskPrio  = 4;
static constexpr BaseType_t kAppTaskCore   = 0;

/** @brief Body of the dedicated application task. */
static void app_task(void *)
{
    app_manager_run();
    /* app_manager_run() only returns when the application asked for a restart. */
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void log_banner(void)
{
    system_info_t info;
    system_info_collect(&info);
    system_info_log_banner(&info);

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

    /* Touch is allowed to fail: the panel may be absent or the FPC loose, and
     * that must not stop the device from booting.  touch_handle() is NULL in
     * that case and no input device is registered. */
    err = touch_init();
    ESP_LOGI(TAG, "Touch      : %s", err == ESP_OK ? "OK" : "NOT DETECTED");

    err = lvgl_port_start();
    ESP_LOGI(TAG, "LVGL       : %s", err == ESP_OK ? "OK" : "FAIL");
    ESP_ERROR_CHECK(err);
    system_info_log_panel();

    if (touch_handle() != NULL) {
        if (touch_attach_lvgl(lvgl_port_display()) == NULL) {
            ESP_LOGW(TAG, "Touch      : driver up but LVGL registration failed");
        }
    }

    /* ---- services ------------------------------------------------------
     * All four are non-fatal by design.  A missing SD card, an RTC that has
     * never been set, and an unconfigured network are normal states of this
     * device, not boot failures - the application has to come up and say so,
     * otherwise there is no way to find out *why* the card is not readable. */
    err = services::storage_init();
    ESP_LOGI(TAG, "SD card    : %s", err == ESP_OK ? "mounted at /sd" : "not mounted");

    err = services::clock_init();
    ESP_LOGI(TAG, "RTC        : %s", err == ESP_OK ? "PCF85063A ok" : "not responding");

    err = services::net_init();
    ESP_LOGI(TAG, "Network    : %s", err == ESP_OK ? "service up" : "unavailable");

    err = services::weather_init();
    ESP_LOGI(TAG, "Weather    : %s", err == ESP_OK ? "service up (sample data)" : "FAIL");

    /* The card usually carries a full GBK face, which the embedded subset is
     * not; installing it is a PSRAM read, so it happens here - after the card
     * is up, and before any page exists to bind a font.  Not finding one is a
     * normal state that leaves the embedded face in place. */
    err = ui::sd_font_install();
    ESP_LOGI(TAG, "CJK font   : %s",
             err == ESP_OK ? "card's GBK face" : "embedded subset (no card face)");

    /* Backlight last: the panel now shows a defined (black) frame, so powering
     * the LED string here cannot flash a white screen.                    */
    if (!lvgl_port_acquire(0)) {
        ESP_LOGE(TAG, "LVGL lock not available");
    }
    ESP_ERROR_CHECK(app_manager_init());
    lvgl_port_release();
    ESP_ERROR_CHECK(display_backlight_set(true));

    system_info_log_memory(TAG);

    /* ---- application task ---------------------------------------------
     * The UI is owned by taskLVGL; this task owns the application lifetime
     * and the health reporting.  Splitting them means a slow heartbeat can
     * never delay a flush, and the scheduler view actually shows who is
     * spending the CPU. */
    if (xTaskCreatePinnedToCore(app_task, "app", kAppTaskStack, nullptr,
                               kAppTaskPrio, nullptr, kAppTaskCore) != pdPASS) {
        ESP_LOGE(TAG, "cannot create the application task");
        esp_restart();
    }

    /* Let the scheduler settle for a moment, then print who is running.  The
     * CPU-share column is the number that matters when the UI feels slow. */
    vTaskDelay(pdMS_TO_TICKS(500));
    system_info_log_tasks();
}
