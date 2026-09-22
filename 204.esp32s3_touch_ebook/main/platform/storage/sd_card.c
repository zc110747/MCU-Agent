/**
 * @file sd_card.c
 * @brief microSD in SPI mode, with the chip select on the I2C expander.
 */

#include "sd_card.h"

#include "board_config.h"
#include "io_expander.h"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd";

/* 20 MHz is comfortably inside spec for the card and the wiring, and it is
 * fast enough that reading a photo does not feel stalled.  Raise it only with
 * a scope on MISO. */
#define SD_MAX_FREQ_KHZ     (20000)
#define SD_MAX_FILES        (8)
#define SD_ALLOC_UNIT       (16 * 1024)
/* Enough for one 512-byte block plus command overhead. */
#define SD_MAX_TRANSFER_SZ  (4000)

static sdmmc_card_t *s_card = NULL;
static bool s_bus_up = false;

esp_err_t sd_card_init(void)
{
    if (s_card != NULL) {
        return ESP_OK;
    }

    /* Assert the expander's CS and hold it: the SPI-SD driver is told there is
     * no CS pin at all (see the header), so this is the only thing driving it.
     * If the mount below fails we release it again so a retry starts clean. */
    ESP_RETURN_ON_ERROR(io_expander_sd_cs(false), TAG, "SD CS assert failed");

    if (!s_bus_up) {
        const spi_bus_config_t bus_cfg = {
            .mosi_io_num = BOARD_SD_MOSI_GPIO,
            .miso_io_num = BOARD_SD_MISO_GPIO,
            .sclk_io_num = BOARD_SD_SCK_GPIO,
            .quadwp_io_num = GPIO_NUM_NC,
            .quadhd_io_num = GPIO_NUM_NC,
            .max_transfer_sz = SD_MAX_TRANSFER_SZ,
        };
        esp_err_t err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
            io_expander_sd_cs(true);
            return err;
        }
        s_bus_up = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;
    host.max_freq_khz = SD_MAX_FREQ_KHZ;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SD_SPI_HOST;
    slot.gpio_cs = SDSPI_SLOT_NO_CS;   /* chip select is on the expander */

    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,   /* never destroy the user's card */
        .max_files = SD_MAX_FILES,
        .allocation_unit_size = SD_ALLOC_UNIT,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot,
                                            &mount_cfg, &s_card);
    if (err != ESP_OK) {
        /* Expected when no card is inserted, so this is a warning and the CS is
         * released to leave the bus idle. */
        ESP_LOGW(TAG, "no card mounted at %s (%s)", SD_MOUNT_POINT, esp_err_to_name(err));
        s_card = NULL;
        io_expander_sd_cs(true);
        return err;
    }

    const sdmmc_card_t *info = s_card;
    ESP_LOGI(TAG, "mounted at %s  %s  %llu MB", SD_MOUNT_POINT, info->cid.name,
             (unsigned long long)((uint64_t)info->csd.capacity * info->csd.sector_size / (1024 * 1024)));
    return ESP_OK;
}

esp_err_t sd_card_deinit(void)
{
    if (s_card == NULL) {
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
    s_card = NULL;
    io_expander_sd_cs(true);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "unmounted");
    }
    return err;
}

bool sd_card_mounted(void)
{
    return s_card != NULL;
}

esp_err_t sd_card_capacity(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_vfs_fat_info(SD_MOUNT_POINT, total_bytes, free_bytes);
}
