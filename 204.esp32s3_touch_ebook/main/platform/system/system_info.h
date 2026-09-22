/**
 * @file system_info.h
 * @brief Platform information gathered at boot (chip, flash, PSRAM, heap).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *chip_model;
    uint32_t    cpu_freq_mhz;
    uint32_t    flash_size_bytes;
    uint32_t    psram_size_bytes;
    uint32_t    heap_internal_free;
    uint32_t    heap_psram_free;
} system_info_t;

/** Fill @p out with the current values and log the boot banner. */
void system_info_collect(system_info_t *out);

/** Log a one-line memory snapshot (internal + PSRAM + LVGL heap). */
void system_info_log_memory(const char *tag);

#ifdef __cplusplus
}
#endif
