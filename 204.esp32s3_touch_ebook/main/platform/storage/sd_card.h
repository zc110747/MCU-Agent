/**
 * @file sd_card.h
 * @brief microSD in SPI mode (Phase 5+ storage).
 *
 * The board wires the card's CS to CH422G EXIO4, NOT to a GPIO.  The SPI-SD
 * driver has an explicit mode for that (`SDSPI_SLOT_NO_CS`): it then configures
 * the SPI peripheral with `spics_io_num = GPIO_NUM_NC` and never touches a chip
 * select itself.  We assert the expander's CS once, at mount time, and leave it
 * asserted - the SD protocol allows CS to stay low across a whole burst of
 * commands, so a single always-selected device on a private bus is fine.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Where the volume is mounted. Used by every storage consumer. */
#define SD_MOUNT_POINT "/sd"

/**
 * @brief Mount the card.
 *
 * Returns an error when no card is present; that is a normal condition the UI
 * has to report, not a fatal one.
 */
esp_err_t sd_card_init(void);

/** @brief Unmount (used when a card is swapped, and by the soak test). */
esp_err_t sd_card_deinit(void);

/** @brief True once sd_card_init() has succeeded. */
bool sd_card_mounted(void);

/**
 * @brief Capacity of the mounted card.
 * @return ESP_ERR_INVALID_STATE when nothing is mounted.
 */
esp_err_t sd_card_capacity(uint64_t *total_bytes, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif
