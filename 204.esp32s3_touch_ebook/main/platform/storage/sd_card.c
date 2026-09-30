/**
 * @file sd_card.c
 * @brief microSD in SPI mode, with the chip select on the I2C expander.
 */

#include "sd_card.h"

#include "board_config.h"
#include "io_expander.h"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include <string.h>

static const char *TAG = "sd";

/* 20 MHz is comfortably inside spec for the card and the wiring, and it is
 * fast enough that reading a photo does not feel stalled.  Raise it only with
 * a scope on MISO. */
#define SD_MAX_FREQ_KHZ     (20000)
#define SD_MAX_FILES        (8)
#define SD_ALLOC_UNIT       (16 * 1024)
/* Enough for one 512-byte block plus command overhead. */
#define SD_MAX_TRANSFER_SZ  (4000)

/* The card has to be clocked at or below 400 kHz until it has answered its
 * identification, so that is also the frequency the wake-up clocks use. */
#define SD_IDLE_FREQ_KHZ    (400)
/* 10 bytes = 80 clock periods.  The SD spec asks for at least 74 clocks with
 * CS de-asserted before CMD0; 80 is the first round number above that. */
#define SD_IDLE_CLOCK_BYTES (10)
/* A card that an earlier reset caught half way through a command answers
 * nothing at all until it is clocked back into a known state.  Retrying costs
 * a couple of hundred milliseconds on a boot with no card in the slot, which
 * nobody notices, and it is the difference between "boot it again" and "pull
 * the power" when a card does get stuck. */
#define SD_MOUNT_ATTEMPTS   (3)

static sdmmc_card_t *s_card = NULL;
static bool s_bus_up = false;

/**
 * @brief Clock the bus with CS de-asserted, to put the card back into SPI mode.
 *
 * The expander owns CS and the SPI driver is registered without a CS pin, so a
 * throw-away device with no CS of its own is enough to emit the clocks: the
 * card sees SCK toggling while its CS is high, which is exactly the wake-up
 * sequence of the specification.
 */
static void sd_bus_idle_clocks(void)
{
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SD_IDLE_FREQ_KHZ * 1000,
        .mode = 0,
        .spics_io_num = GPIO_NUM_NC,   /* the expander holds CS high for us */
        .queue_size = 1,
    };

    spi_device_handle_t dev = NULL;
    if (spi_bus_add_device(BOARD_SD_SPI_HOST, &dev_cfg, &dev) != ESP_OK) {
        return;
    }

    uint8_t tx[SD_IDLE_CLOCK_BYTES];
    memset(tx, 0xFF, sizeof(tx));

    spi_transaction_t t = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
    };
    spi_device_polling_transmit(dev, &t);
    spi_bus_remove_device(dev);
}

esp_err_t sd_card_init(void)
{
    if (s_card != NULL) {
        return ESP_OK;
    }

    /* The bus comes up before the first attempt because the wake-up clocks
     * need it, and it stays up across the retries. */
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

    esp_err_t err = ESP_FAIL;

    for (int attempt = 1; attempt <= SD_MOUNT_ATTEMPTS; ++attempt) {
        /* CS high while the clocks run, then CS low for the command stream.
         * Doing this on every attempt - not only the first - is what recovers
         * a card that an earlier reset left selected and mid-command: a chip
         * reset does not cut the card's supply, so nothing else clears it. */
        if (io_expander_sd_cs(true) != ESP_OK) {
            ESP_LOGE(TAG, "SD CS de-assert failed");
            return ESP_FAIL;
        }
        sd_bus_idle_clocks();
        if (io_expander_sd_cs(false) != ESP_OK) {
            ESP_LOGE(TAG, "SD CS assert failed");
            return ESP_FAIL;
        }

        err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot,
                                      &mount_cfg, &s_card);
        if (err == ESP_OK) {
            const sdmmc_card_t *info = s_card;
            ESP_LOGI(TAG, "mounted at %s  %s  %llu MB", SD_MOUNT_POINT, info->cid.name,
                     (unsigned long long)((uint64_t)info->csd.capacity * info->csd.sector_size / (1024 * 1024)));
            if (attempt > 1) {
                ESP_LOGW(TAG, "the card needed %d attempts to come up", attempt);
            }
            return ESP_OK;
        }

        /* A warning, not an error: no card in the slot is a normal state of
         * this device.  The mount path frees its own state on failure, so the
         * next attempt starts from a clean slot; CS is released in between so
         * the card sees an idle bus rather than a permanent select. */
        s_card = NULL;
        ESP_LOGW(TAG, "mount attempt %d/%d failed (%s)",
                 attempt, SD_MOUNT_ATTEMPTS, esp_err_to_name(err));
        io_expander_sd_cs(true);
    }

    ESP_LOGW(TAG, "no card mounted at %s (%s)", SD_MOUNT_POINT, esp_err_to_name(err));
    return err;
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
