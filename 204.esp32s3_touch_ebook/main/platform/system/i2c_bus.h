/**
 * @file i2c_bus.h
 * @brief Shared I2C master bus (GPIO8/GPIO9) used by CH422G, GT911 and PCF85063.
 */
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create the shared I2C master bus. Safe to call more than once. */
esp_err_t i2c_bus_init(void);

/** Bus handle, or NULL when i2c_bus_init() has not run / failed. */
i2c_master_bus_handle_t i2c_bus_handle(void);

/**
 * @brief Register a device on the shared bus.
 * @param dev_addr 7-bit address.
 */
esp_err_t i2c_bus_add_device(uint8_t dev_addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out);

#ifdef __cplusplus
}
#endif
