#include "i2c_bus.h"
#include "board_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus = NULL;
static SemaphoreHandle_t s_lock = NULL;

esp_err_t i2c_bus_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    const i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
            .allow_pd = 0,
        },
    };

    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        s_bus = NULL;
        return err;
    }
    ESP_LOGI(TAG, "bus ready  SDA=IO%d SCL=IO%d @%dkHz",
             (int)BOARD_I2C_SDA_GPIO, (int)BOARD_I2C_SCL_GPIO,
             BOARD_I2C_FREQ_HZ / 1000);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_handle(void)
{
    return s_bus;
}

esp_err_t i2c_bus_add_device(uint8_t dev_addr, uint32_t scl_speed_hz, i2c_master_dev_handle_t *out)
{
    if (!s_bus || !out) {
        return ESP_ERR_INVALID_STATE;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = scl_speed_hz,
        .scl_wait_us = 0,
        .flags = { .disable_ack_check = 0 },
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, out);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "add device 0x%02X failed: %s", dev_addr, esp_err_to_name(err));
    }
    return err;
}
