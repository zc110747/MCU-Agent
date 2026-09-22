#include "system_info.h"
#include "board_config.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sysinfo";

void system_info_collect(system_info_t *out)
{
    if (!out) {
        return;
    }

    out->chip_model = BOARD_MODULE;
    out->cpu_freq_mhz = (uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;

    out->flash_size_bytes = 0;
    if (esp_flash_get_size(NULL, &out->flash_size_bytes) != ESP_OK) {
        out->flash_size_bytes = 0;
    }
    out->psram_size_bytes = (uint32_t)esp_psram_get_size();
    out->heap_internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->heap_psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

void system_info_log_banner(const system_info_t *info)
{
    system_info_t local;
    if (info == NULL) {
        system_info_collect(&local);
        info = &local;
    }

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "chip            : %s rev v%d.%d, %d core(s), CPU %u MHz",
             info->chip_model, chip.revision / 100, chip.revision % 100,
             chip.cores, (unsigned)info->cpu_freq_mhz);
    ESP_LOGI(TAG, "flash           : %u MB", (unsigned)(info->flash_size_bytes / (1024 * 1024)));
    ESP_LOGI(TAG, "psram           : %u MB", (unsigned)(info->psram_size_bytes / (1024 * 1024)));
}

void system_info_log_memory(const char *tag)
{
    char buf[192];
    snprintf(buf, sizeof(buf),
             "heap int=%u B  psram=%u B  int_largest=%u B  psram_largest=%u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    ESP_LOGI(tag ? tag : TAG, "%s", buf);
}

void system_info_log_tasks(void)
{
#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
    /* Both formatters build the whole table into a caller-supplied buffer and
     * neither reports overflow, so this is deliberately oversized: ~24 tasks
     * at ~64 characters each.  A truncated table would read as a task that
     * vanished.  Allocating it in PSRAM keeps the 4 KB out of internal RAM. */
    const size_t cap = 4096;
    char *buf = (char *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        buf = (char *)malloc(cap);
    }
    if (buf == NULL) {
        ESP_LOGW(TAG, "no buffer for the task table");
        return;
    }

    /* vTaskList: name / state / priority / stack high-water / task# (+ core
     * id when CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y).  Stack high-water
     * is the *minimum* free seen, so a number close to zero is the warning. */
    buf[0] = '\0';
    vTaskList(buf);
    ESP_LOGI(TAG, "task table -- name | state | prio | stack high-water | task# | core");
    ESP_LOGI(TAG, "%s", buf);

    /* vTaskGetRunTimeStats: absolute CPU time and its share.  This is the one
     * that answers "is taskLVGL actually being scheduled?". */
    buf[0] = '\0';
    vTaskGetRunTimeStats(buf);
    ESP_LOGI(TAG, "cpu share -- name | abs time | %%");
    ESP_LOGI(TAG, "%s", buf);

    free(buf);
#else
    ESP_LOGW(TAG, "task table needs CONFIG_FREERTOS_USE_TRACE_FACILITY and "
                  "CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS");
#endif
}

void system_info_log_panel(void)
{
    /* Frame rate = pclk / (total pixels per line * total lines per frame).
     * Blanking is part of both totals, so the porches gate the refresh rate
     * exactly as much as the visible area does - which is why a "laggy panel"
     * is worth checking here before anything is blamed on LVGL. */
    const uint32_t htotal = (uint32_t)BOARD_LCD_H_RES + BOARD_LCD_HSYNC_PULSE_WIDTH +
                            BOARD_LCD_HSYNC_BACK_PORCH + BOARD_LCD_HSYNC_FRONT_PORCH;
    const uint32_t vtotal = (uint32_t)BOARD_LCD_V_RES + BOARD_LCD_VSYNC_PULSE_WIDTH +
                            BOARD_LCD_VSYNC_BACK_PORCH + BOARD_LCD_VSYNC_FRONT_PORCH;
    const uint32_t pclk = BOARD_LCD_PCLK_HZ;

    const uint32_t panel_hz = pclk / (htotal * vtotal);
    ESP_LOGI(TAG, "panel      : %ux%u pclk %u MHz  htotal %u  vtotal %u  -> %u Hz",
             (unsigned)BOARD_LCD_H_RES, (unsigned)BOARD_LCD_V_RES,
             (unsigned)(pclk / 1000000u), (unsigned)htotal, (unsigned)vtotal,
             (unsigned)panel_hz);
    /* LVGL's refresh period is a request, not a guarantee: it also sets the
     * input-device read period.  In direct mode the flush blocks the LVGL task
     * until the panel hands the frame buffer back
     * (esp_lvgl_port_disp.c: "Waiting for the last frame buffer to complete
     * transmission" - xSemaphoreTake(trans_sem, portMAX_DELAY)), so when this
     * value is shorter than one panel frame LVGL asks for frames the panel
     * cannot deliver.  The ratio below is the honest description of the UI
     * update rate; do not read the left-hand number as a frame rate. */
    ESP_LOGI(TAG, "lvgl       : refresh period %d ms (asks %.1f Hz, panel answers %u Hz"
                  " -> update rate is panel-bound)",
             (int)CONFIG_LV_DEF_REFR_PERIOD, 1000.0 / (double)CONFIG_LV_DEF_REFR_PERIOD,
             (unsigned)panel_hz);
}
