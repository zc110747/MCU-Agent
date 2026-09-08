/**
 * @file target.h
 * @brief Target MCU detection (DPIDR / AP IDR / CPUID / ROM table).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t dpidr;             /* Debug Port ID register                */
    uint32_t ap_idr;            /* MEM-AP identification register        */
    uint32_t cpuid;             /* CPUID (0 if unreadable)               */
    uint32_t rom_table;         /* ROM table base (0 if unreadable)      */
    uint16_t cpu_partno;        /* CPUID PARTNO field (0 if unknown)     */
    const char *core_name;      /* "Cortex-M4" etc., "Unknown" fallback  */
    bool debug_connected;       /* core debug logic powered + halted?    */
} target_info_t;

/**
 * @brief Detect the connected target. Must be called after debug_connect().
 */
esp_err_t target_detect(target_info_t *info);

/**
 * @brief Convenience: connect + detect + log a summary. Returns esp_err_t.
 */
esp_err_t target_connect_and_identify(target_info_t *info);

#ifdef __cplusplus
}
#endif
