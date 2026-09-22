#include "system_info.h"
#include "board_config.h"

#include <stdio.h>
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "sdkconfig.h"

static const char *TAG = "sysinfo";

void system_info_collect(system_info_t *out)
{
    if (!out) {
        return;
    }

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    out->chip_model = BOARD_MODULE;
    out->cpu_freq_mhz = (uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;

    out->flash_size_bytes = 0;
    if (esp_flash_get_size(NULL, &out->flash_size_bytes) != ESP_OK) {
        out->flash_size_bytes = 0;
    }
    out->psram_size_bytes = (uint32_t)esp_psram_get_size();
    out->heap_internal_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->heap_psram_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "chip            : %s rev v%d.%d, %d core(s), CPU %u MHz",
             out->chip_model, chip.revision / 100, chip.revision % 100,
             chip.cores, (unsigned)out->cpu_freq_mhz);
    ESP_LOGI(TAG, "flash           : %u MB", (unsigned)(out->flash_size_bytes / (1024 * 1024)));
    ESP_LOGI(TAG, "psram           : %u MB", (unsigned)(out->psram_size_bytes / (1024 * 1024)));
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

    if (tag) {
        ESP_LOGI(tag, "%s", buf);
    } else {
        ESP_LOGI(TAG, "%s", buf);
    }
}
