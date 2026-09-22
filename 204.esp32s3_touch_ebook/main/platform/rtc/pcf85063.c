/**
 * @file pcf85063.c
 * @brief PCF85063A RTC driver.
 *
 * The register block is contiguous and auto-incrementing, so one read gets the
 * whole time (seconds..years) and one write sets it.  Values are BCD, and the
 * seconds register carries an oscillator-stop flag in bit 7 which is the only
 * way to tell "never set" from "set to 00:00:00".
 */

#include "pcf85063.h"

#include "board_config.h"
#include "i2c_bus.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "rtc";

/* Register addresses (datasheet section 8). */
#define REG_CONTROL_1   (0x00)
#define REG_SECONDS     (0x02)   /* .. 0x08 = years, contiguous */

#define TIME_BYTES      (7)

#define I2C_TIMEOUT_MS  (100)

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_valid = false;

static inline uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t pcf85063_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2c_bus_add_device(BOARD_PCF85063_I2C_ADDR, 100 * 1000, &s_dev),
                        TAG, "registering the RTC on the shared bus failed");

    /* Probe by reading the seconds register; a missing chip answers NACK and
     * this is where we find out, rather than on the first page that needs the
     * time. */
    uint8_t reg = REG_SECONDS;
    uint8_t seconds = 0;
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, &seconds, 1, I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no PCF85063 at 0x%02X (%s)", (unsigned)BOARD_PCF85063_I2C_ADDR,
                 esp_err_to_name(err));
        s_dev = NULL;
        return err;
    }

    /* CONTROL_1 = 0 : normal mode (not STOP), 24-hour, no correction pulse. */
    const uint8_t init[2] = {REG_CONTROL_1, 0x00};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, init, sizeof(init), I2C_TIMEOUT_MS),
                        TAG, "CONTROL_1 write failed");

    s_valid = (seconds & 0x80) == 0;
    ESP_LOGI(TAG, "PCF85063 ready at 0x%02X  time %s",
             (unsigned)BOARD_PCF85063_I2C_ADDR,
             s_valid ? "is valid" : "was never set (oscillator stop flag set)");
    return ESP_OK;
}

esp_err_t pcf85063_get(struct tm *out)
{
    if (s_dev == NULL || out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t reg = REG_SECONDS;
    uint8_t raw[TIME_BYTES] = {0};
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, raw, TIME_BYTES, I2C_TIMEOUT_MS),
                        TAG, "time read failed");

    s_valid = (raw[0] & 0x80) == 0;

    *out = (struct tm){0};
    out->tm_sec  = bcd2bin(raw[0] & 0x7F);
    out->tm_min  = bcd2bin(raw[1] & 0x7F);
    out->tm_hour = bcd2bin(raw[2] & 0x3F);   /* bit 6 selects 12 h; masked off */
    out->tm_mday = bcd2bin(raw[3] & 0x3F);
    /* raw[4] is the weekday counter, not used: tm_wday is derived instead. */
    out->tm_mon  = bcd2bin(raw[5] & 0x1F) - 1;   /* struct tm months are 0-based */
    out->tm_year = bcd2bin(raw[6]) + 100;        /* chip stores 2000-based years */

    /* Normalise (and fill tm_wday/tm_yday) through mktime, then put the
     * normalised values back. */
    time_t epoch = mktime(out);
    if (epoch != (time_t)-1) {
        struct tm norm;
        localtime_r(&epoch, &norm);
        *out = norm;
    }
    return ESP_OK;
}

esp_err_t pcf85063_set(const struct tm *in)
{
    if (s_dev == NULL || in == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm copy = *in;
    time_t epoch = mktime(&copy);
    if (epoch == (time_t)-1) {
        return ESP_ERR_INVALID_ARG;
    }
    struct tm norm;
    localtime_r(&epoch, &norm);

    const uint8_t buf[TIME_BYTES + 1] = {
        REG_SECONDS,
        bin2bcd((uint8_t)norm.tm_sec),
        bin2bcd((uint8_t)norm.tm_min),
        bin2bcd((uint8_t)norm.tm_hour),
        bin2bcd((uint8_t)norm.tm_mday),
        bin2bcd((uint8_t)(norm.tm_wday + 1)),      /* chip counts 1..7 */
        bin2bcd((uint8_t)(norm.tm_mon + 1)),
        bin2bcd((uint8_t)(norm.tm_year - 100)),
    };

    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS),
                        TAG, "time write failed");
    s_valid = true;
    return ESP_OK;
}

bool pcf85063_time_valid(void)
{
    return s_valid;
}
