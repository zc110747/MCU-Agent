/**
 * @file debug_engine.c
 * @brief Cortex-M debug architecture (DP / AP / MEM-AP / core debug regs).
 */
#include "debug_engine.h"

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_rom_sys.h"
#include "swd.h"
#include "jtag.h"
#include "esp_log.h"

static const char *TAG = "debug";

static SemaphoreHandle_t s_mutex = NULL;
static bool s_connected = false;

/* ------------------------------------------------------------------ */
/* Wireless-debug RAM reservation (external PSRAM)                      */
/* ------------------------------------------------------------------ */
#if CONFIG_WIRELESS_DEBUG_RESERVE_RAM
#include "esp_heap_caps.h"
static void *s_wireless_debug_pool = NULL;

static void wireless_debug_reserve_init(void)
{
    size_t size = (size_t)CONFIG_WIRELESS_DEBUG_RESERVE_RAM_SIZE_KB * 1024u;
    /* Carve the pool out of external PSRAM and keep it for the whole lifetime
     * of the firmware - it is intentionally NEVER freed. This guarantees a
     * contiguous buffer region for the future DAP-over-WiFi transport, away
     * from the scarce internal DRAM that the bit-bang IRAM code needs. */
    s_wireless_debug_pool = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_wireless_debug_pool) {
        ESP_LOGI(TAG, "wireless-debug pool reserved: %u KB @ %p (PSRAM)",
                 (unsigned)CONFIG_WIRELESS_DEBUG_RESERVE_RAM_SIZE_KB,
                 s_wireless_debug_pool);
    } else {
        ESP_LOGW(TAG, "wireless-debug pool reserve FAILED (PSRAM unavailable?)");
    }
}
#endif

esp_err_t debug_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = swd_init();
    if (err != ESP_OK) {
        return err;
    }
    err = jtag_init();
    if (err != ESP_OK) {
        return err;
    }
#if CONFIG_WIRELESS_DEBUG_RESERVE_RAM
    wireless_debug_reserve_init();
#endif
    return ESP_OK;
}

esp_err_t debug_engine_lock(uint32_t timeout_ms)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    TickType_t ticks = (timeout_ms == UINT32_MAX)
                       ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake(s_mutex, ticks) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void debug_engine_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

esp_err_t debug_connect(void)
{
    esp_err_t err = swd_connect();
    if (err != ESP_OK) {
        return err;
    }

    /* Power up debug + system domains */
    uint32_t ctrlstat = 0;
    err = swd_write_dp(SWD_DP_ADDR_CTRLSTAT, DP_CDBGPWRUPREQ | DP_CSYSPWRUPREQ);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < 100; i++) {
        err = swd_read_dp(SWD_DP_ADDR_CTRLSTAT, &ctrlstat);
        if (err == ESP_OK &&
            (ctrlstat & (DP_CDBGPWRUPACK | DP_CSYSPWRUPACK)) == (DP_CDBGPWRUPACK | DP_CSYSPWRUPACK)) {
            break;
        }
        esp_rom_delay_us(1000);
    }
    if (err != ESP_OK ||
        (ctrlstat & (DP_CDBGPWRUPACK | DP_CSYSPWRUPACK)) != (DP_CDBGPWRUPACK | DP_CSYSPWRUPACK)) {
        ESP_LOGW(TAG, "DP power-up failed (CTRLSTAT=0x%08" PRIx32 ")", ctrlstat);
        return ESP_ERR_NOT_FOUND;
    }

    /* Clear sticky errors, select AP 0 / bank 0 */
    swd_write_dp(SWD_DP_ADDR_ABORT, 0x0000001Fu);
    err = swd_write_dp(SWD_DP_ADDR_SELECT, 0x00000000u);
    if (err != ESP_OK) {
        return err;
    }

    s_connected = true;
    ESP_LOGI(TAG, "target powered up (CTRLSTAT=0x%08" PRIx32 ")", ctrlstat);
    return ESP_OK;
}

esp_err_t debug_disconnect(void)
{
    /* Power down (optional for host; keep DP powered is also fine, but
     * a clean disconnect releases the bus) */
    swd_write_dp(SWD_DP_ADDR_CTRLSTAT, 0);
    swd_set_idle();
    s_connected = false;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Halt / Run / Step / Reset                                           */
/* ------------------------------------------------------------------ */
esp_err_t debug_halt(void)
{
    /* Enable debug + request halt */
    esp_err_t err = debug_write_word(DHCSR_ADDR, DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_HALT);
    if (err != ESP_OK) {
        return err;
    }
    uint32_t dhcsr = 0;
    for (int i = 0; i < 100; i++) {
        err = debug_read_word(DHCSR_ADDR, &dhcsr);
        if (err == ESP_OK && (dhcsr & DHCSR_S_HALT)) {
            ESP_LOGI(TAG, "core halted (DHCSR=0x%08" PRIx32 ")", dhcsr);
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t debug_run(void)
{
    return debug_write_word(DHCSR_ADDR, DHCSR_DBGKEY | DHCSR_C_DEBUGEN);
}

esp_err_t debug_step(void)
{
    bool halted = false;
    esp_err_t err = debug_is_halted(&halted);
    if (err != ESP_OK) {
        return err;
    }
    if (!halted) {
        return ESP_ERR_INVALID_STATE;
    }
    err = debug_write_word(DHCSR_ADDR, DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_STEP);
    if (err != ESP_OK) {
        return err;
    }
    uint32_t dhcsr = 0;
    for (int i = 0; i < 100; i++) {
        err = debug_read_word(DHCSR_ADDR, &dhcsr);
        if (err == ESP_OK && (dhcsr & DHCSR_S_HALT)) {
            return ESP_OK;
        }
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t debug_is_halted(bool *halted)
{
    uint32_t dhcsr = 0;
    esp_err_t err = debug_read_word(DHCSR_ADDR, &dhcsr);
    if (err == ESP_OK) {
        *halted = (dhcsr & DHCSR_S_HALT) != 0;
    }
    return err;
}

esp_err_t debug_reset(bool hard_reset)
{
    esp_err_t err;
    if (hard_reset) {
        err = swd_reset_assert(true);
        if (err != ESP_OK) {
            /* No nRESET wired: fall back to SYSRESETREQ */
            hard_reset = false;
        } else {
            esp_rom_delay_us(50000);
            swd_reset_assert(false);
            esp_rom_delay_us(50000);
            /* Re-establish the SWD link after the target rebooted */
            err = swd_connect();
            if (err == ESP_OK) {
                return debug_connect();
            }
            return err;
        }
    }
    if (!hard_reset) {
        err = debug_write_word(AIRCR_ADDR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ);
        if (err != ESP_OK) {
            return err;
        }
        esp_rom_delay_us(100000);
        /* DP stays powered through a system reset; re-select AP bank */
        swd_write_dp(SWD_DP_ADDR_SELECT, 0x00000000u);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Core registers (target must be halted)                              */
/* ------------------------------------------------------------------ */
esp_err_t debug_read_register(uint32_t reg, uint32_t *value)
{
    esp_err_t err = debug_write_word(DCRSR_ADDR, reg & 0x1Fu);
    if (err != ESP_OK) {
        return err;
    }
    uint32_t dhcsr = 0;
    for (int i = 0; i < 100; i++) {
        err = debug_read_word(DHCSR_ADDR, &dhcsr);
        if (err == ESP_OK && (dhcsr & DHCSR_S_REGRDY)) {
            return debug_read_word(DCRDR_ADDR, value);
        }
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t debug_write_register(uint32_t reg, uint32_t value)
{
    esp_err_t err = debug_write_word(DCRDR_ADDR, value);
    if (err != ESP_OK) {
        return err;
    }
    return debug_write_word(DCRSR_ADDR, (reg & 0x1Fu) | (1uL << 16));
}

/* ------------------------------------------------------------------ */
/* Memory access via MEM-AP                                            */
/* ------------------------------------------------------------------ */
/* Helper: select CSW size, write TAR, then access DRW */
static esp_err_t mem_ap_config(uint32_t csw_size, bool autoinc)
{
    uint32_t csw = CSW_BASE | csw_size |
                   (autoinc ? CSW_ADDRINC_AUTO : CSW_ADDRINC_SINGLE);
    return swd_write_ap(MEM_AP_CSW, csw);
}

esp_err_t debug_read_word(uint32_t address, uint32_t *value)
{
    esp_err_t err = mem_ap_config(CSW_SIZE_32, false);
    if (err != ESP_OK) {
        return err;
    }
    err = swd_write_ap(MEM_AP_TAR, address);
    if (err != ESP_OK) {
        return err;
    }
    return swd_read_ap(MEM_AP_DRW, value);
}

esp_err_t debug_write_word(uint32_t address, uint32_t value)
{
    esp_err_t err = mem_ap_config(CSW_SIZE_32, false);
    if (err != ESP_OK) {
        return err;
    }
    err = swd_write_ap(MEM_AP_TAR, address);
    if (err != ESP_OK) {
        return err;
    }
    return swd_write_ap(MEM_AP_DRW, value);
}

esp_err_t debug_read_memory(uint32_t address, void *buffer, size_t size)
{
    if (!s_connected || buffer == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t *dst = (uint8_t *)buffer;
    esp_err_t err = ESP_OK;

    while (size > 0) {
        /* Split at 1 KB boundaries: TAR autoincrement wraps within 1 KB */
        size_t chunk = size;
        size_t to_boundary = 0x400u - (address & 0x3FFu);
        if (chunk > to_boundary) {
            chunk = to_boundary;
        }

        uint32_t csw_size;
        size_t width;
        if (((address | chunk) & 0x3u) == 0) {
            csw_size = CSW_SIZE_32; width = 4;
        } else if (((address | chunk) & 0x1u) == 0) {
            csw_size = CSW_SIZE_16; width = 2;
        } else {
            csw_size = CSW_SIZE_8;  width = 1;
        }

        err = mem_ap_config(csw_size, true);
        if (err != ESP_OK) break;
        err = swd_write_ap(MEM_AP_TAR, address);
        if (err != ESP_OK) break;

        size_t n = chunk / width;
        for (size_t i = 0; i < n; i++) {
            uint32_t data = 0;
            err = swd_read_ap(MEM_AP_DRW, &data);
            if (err != ESP_OK) goto out;
            if (width == 4) {
                memcpy(dst, &data, 4);
            } else if (width == 2) {
                uint16_t h = (uint16_t)data;
                memcpy(dst, &h, 2);
            } else {
                *dst = (uint8_t)data;
            }
            dst += width;
        }
        address += chunk;
        size -= chunk;
    }
out:
    return err;
}

esp_err_t debug_write_memory(uint32_t address, const void *buffer, size_t size)
{
    if (!s_connected || buffer == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *src = (const uint8_t *)buffer;
    esp_err_t err = ESP_OK;

    while (size > 0) {
        size_t chunk = size;
        size_t to_boundary = 0x400u - (address & 0x3FFu);
        if (chunk > to_boundary) {
            chunk = to_boundary;
        }

        uint32_t csw_size;
        size_t width;
        if (((address | chunk) & 0x3u) == 0) {
            csw_size = CSW_SIZE_32; width = 4;
        } else if (((address | chunk) & 0x1u) == 0) {
            csw_size = CSW_SIZE_16; width = 2;
        } else {
            csw_size = CSW_SIZE_8;  width = 1;
        }

        err = mem_ap_config(csw_size, true);
        if (err != ESP_OK) break;
        err = swd_write_ap(MEM_AP_TAR, address);
        if (err != ESP_OK) break;

        size_t n = chunk / width;
        for (size_t i = 0; i < n; i++) {
            uint32_t data;
            if (width == 4) {
                memcpy(&data, src, 4);
            } else if (width == 2) {
                uint16_t h; memcpy(&h, src, 2); data = h;
            } else {
                data = *src;
            }
            err = swd_write_ap(MEM_AP_DRW, data);
            if (err != ESP_OK) goto out;
            src += width;
        }
        address += chunk;
        size -= chunk;
    }
out:
    return err;
}
