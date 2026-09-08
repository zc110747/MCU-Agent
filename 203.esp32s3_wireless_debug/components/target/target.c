/**
 * @file target.c
 * @brief Target detection: DPIDR -> AP IDR -> ROM table -> CPUID.
 */
#include "target.h"

#include <string.h>
#include <inttypes.h>
#include "swd.h"
#include "debug_engine.h"
#include "esp_log.h"

static const char *TAG = "target";

/* Cortex-M CPUID PARTNO (bits [15:4]) */
#define CORTEX_M0_PARTNO    0xC20u
#define CORTEX_M0P_PARTNO   0xC60u
#define CORTEX_M3_PARTNO    0xC23u
#define CORTEX_M4_PARTNO    0xC24u
#define CORTEX_M7_PARTNO    0xC27u
#define CORTEX_M23_PARTNO   0xD20u
#define CORTEX_M33_PARTNO   0xD21u

static const char *partno_to_name(uint16_t partno)
{
    switch (partno) {
    case CORTEX_M0_PARTNO:  return "Cortex-M0";
    case CORTEX_M0P_PARTNO: return "Cortex-M0+";
    case CORTEX_M3_PARTNO:  return "Cortex-M3";
    case CORTEX_M4_PARTNO:  return "Cortex-M4";
    case CORTEX_M7_PARTNO:  return "Cortex-M7";
    case CORTEX_M23_PARTNO: return "Cortex-M23";
    case CORTEX_M33_PARTNO: return "Cortex-M33";
    default:                return "Unknown";
    }
}

esp_err_t target_detect(target_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));
    info->core_name = "Unknown";

    /* DPIDR (also validates the SWD link) */
    uint32_t dpidr = 0;
    esp_err_t err = swd_read_dp(SWD_DP_ADDR_IDCODE, &dpidr);
    if (err != ESP_OK || dpidr == 0) {
        ESP_LOGW(TAG, "DPIDR read failed (err=%s, val=0x%08" PRIx32 ")",
                 esp_err_to_name(err), dpidr);
        return ESP_ERR_NOT_FOUND;
    }
    info->dpidr = dpidr;

    /* MEM-AP IDR (APSEL=0, register 0xFC) */
    uint32_t ap_idr = 0;
    if (swd_read_ap(MEM_AP_IDR, &ap_idr) == ESP_OK) {
        info->ap_idr = ap_idr;
    }

    /* CPUID */
    uint32_t cpuid = 0;
    if (debug_read_word(CPUID_ADDR, &cpuid) == ESP_OK && cpuid != 0) {
        info->cpuid = cpuid;
        info->cpu_partno = (uint16_t)((cpuid >> 4) & 0xFFFu);
        info->core_name = partno_to_name(info->cpu_partno);
    }

    /* ROM table (Cortex-M3/M4/M7 classic architecture) */
    uint32_t rom = 0;
    if (debug_read_word(0xE00FF000u, &rom) == ESP_OK) {
        info->rom_table = rom;
    }

    ESP_LOGI(TAG, "Target connected");
    ESP_LOGI(TAG, "  DPIDR     = 0x%08" PRIX32, info->dpidr);
    ESP_LOGI(TAG, "  AP IDR    = 0x%08" PRIX32, info->ap_idr);
    ESP_LOGI(TAG, "  CPUID     = 0x%08" PRIX32, info->cpuid);
    ESP_LOGI(TAG, "  ROM table = 0x%08" PRIX32, info->rom_table);
    ESP_LOGI(TAG, "  Core      = %s", info->core_name);
    return ESP_OK;
}

esp_err_t target_connect_and_identify(target_info_t *info)
{
    esp_err_t err = debug_connect();
    if (err != ESP_OK) {
        return err;
    }
    return target_detect(info);
}
