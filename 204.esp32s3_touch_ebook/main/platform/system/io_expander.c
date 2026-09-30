#include "io_expander.h"
#include "i2c_bus.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ch422g";

/* CH422G command words live in the I2C *address* phase:
 *   0x24  write, set system parameter
 *   0x38  write, bidirectional I/O output register (IO7..IO0)
 *   0x46  write, general purpose / open-drain outputs (OC3..OC0)
 *   0x26  read,  bidirectional I/O pin state
 * See CH422DS1_EN.pdf sections 6.1 / 6.2 / 6.3 / 6.4.                     */
#define CH422G_REG_MODE   (0x24)
#define CH422G_REG_OUT    (0x38)
#define CH422G_REG_READ   (0x26)

/* Byte 2 of the "set system parameter" command:
 *   [SLEEP] 0 0 [OD_EN] 0 [A_SCAN] 0 [IO_OE]
 * 0x01 -> I/O extension mode, IO0..IO7 are push-pull outputs,
 *         OC0..OC3 stay push-pull, automatic LED scan off, no sleep.
 * Use 0x11 instead if the OC pins must be open-drain.                    */
#define CH422G_MODE_IO_OUT_PP (0x01)

/* Power-on / reset safe state: both panel and touch held in reset,
 * backlight off, SD chip select de-asserted (high).                      */
#define CH422G_SAFE_VALUE                                                      \
    ((0 << BOARD_EXIO_DI0) | (0 << BOARD_EXIO_CTP_RST) | (0 << BOARD_EXIO_DISP) | \
     (0 << BOARD_EXIO_LCD_RST) | (1 << BOARD_EXIO_SD_CS) | (0 << BOARD_EXIO_DI1))

static i2c_master_dev_handle_t s_dev_mode;
static i2c_master_dev_handle_t s_dev_out;
static i2c_master_dev_handle_t s_dev_read;
static SemaphoreHandle_t s_lock;
static uint8_t s_shadow;
static bool s_ready;

static esp_err_t ch422g_raw_write(i2c_master_dev_handle_t dev, uint8_t value)
{
    return i2c_master_transmit(dev, &value, 1, 100);
}

static esp_err_t ch422g_apply(uint8_t value)
{
    esp_err_t err = ch422g_raw_write(s_dev_out, value);
    if (err == ESP_OK) {
        s_shadow = value;
    } else {
        ESP_LOGE(TAG, "write 0x%02X failed: %s", value, esp_err_to_name(err));
    }
    return err;
}

esp_err_t io_expander_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_bus_add_device(CH422G_REG_MODE, BOARD_I2C_FREQ_HZ, &s_dev_mode);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_bus_add_device(CH422G_REG_OUT, BOARD_I2C_FREQ_HZ, &s_dev_out);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_bus_add_device(CH422G_REG_READ, BOARD_I2C_FREQ_HZ, &s_dev_read);
    if (err != ESP_OK) {
        return err;
    }

    /* 1. enable the outputs */
    err = ch422g_raw_write(s_dev_mode, CH422G_MODE_IO_OUT_PP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mode write failed: %s", esp_err_to_name(err));
        return err;
    }

    /* 2. drive everything into a known safe state */
    s_shadow = 0x00; /* force a real write even if the shadow already matches */
    err = ch422g_apply(CH422G_SAFE_VALUE);
    if (err != ESP_OK) {
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "ready  addr=0x%02X  out=0x%02X", CH422G_REG_MODE, s_shadow);
    return ESP_OK;
}

esp_err_t io_expander_write(uint8_t value)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ch422g_apply(value);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t io_expander_set_level(board_exio_t pin, bool level)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pin < 0 || pin >= BOARD_EXIO_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t next = s_shadow;
    if (level) {
        next |= (uint8_t)(1u << pin);
    } else {
        next &= (uint8_t)~(1u << pin);
    }
    esp_err_t err = (next == s_shadow) ? ESP_OK : ch422g_apply(next);
    xSemaphoreGive(s_lock);
    return err;
}

bool io_expander_get_level(board_exio_t pin)
{
    if (pin < 0 || pin >= BOARD_EXIO_COUNT) {
        return false;
    }
    return (s_shadow & (1u << pin)) != 0;
}

uint8_t io_expander_get_value(void)
{
    return s_shadow;
}

esp_err_t io_expander_lcd_reset(bool level)
{
    return io_expander_set_level(BOARD_EXIO_LCD_RST, level);
}

esp_err_t io_expander_touch_reset(bool level)
{
    return io_expander_set_level(BOARD_EXIO_CTP_RST, level);
}

esp_err_t io_expander_backlight_enable(bool on)
{
    return io_expander_set_level(BOARD_EXIO_DISP, on);
}

esp_err_t io_expander_sd_cs(bool level)
{
    return io_expander_set_level(BOARD_EXIO_SD_CS, level);
}
