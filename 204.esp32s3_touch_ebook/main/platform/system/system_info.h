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

/**
 * @brief Fill @p out with the current values. Emits no log output.
 *
 * Collection and logging are deliberately separate calls. Pages poll this from
 * a status timer - a version that logged on every call turned a 2 s footer
 * refresh into a three-line banner every 2 s, forever, which buries the log
 * you actually want to read.
 */
void system_info_collect(system_info_t *out);

/** Log the three-line boot banner for @p info (or collect and log if NULL). */
void system_info_log_banner(const system_info_t *info);

/** Log a one-line memory snapshot (internal + PSRAM + largest free blocks). */
void system_info_log_memory(const char *tag);

/**
 * @brief Log the live FreeRTOS task table: name, state, priority, core,
 *        stack high-water mark and CPU share.
 *
 * The whole point of running under FreeRTOS is that the scheduling is
 * inspectable, so this prints the actual scheduler state rather than the
 * intended one.  Two things it is used to answer:
 *   - "is the UI task getting the CPU it needs?" (a low % for taskLVGL while
 *     something else is hot is where input lag comes from)
 *   - "is any stack close to its limit?" (high-water is the minimum ever free)
 *
 * Requires CONFIG_FREERTOS_USE_TRACE_FACILITY and
 * CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS; it is compiled out when
 * they are off, so it can stay in the delivered firmware.
 */
void system_info_log_tasks(void);

/** @brief Log the RGB panel's refresh rate implied by board_config timing. */
void system_info_log_panel(void);

#ifdef __cplusplus
}
#endif
