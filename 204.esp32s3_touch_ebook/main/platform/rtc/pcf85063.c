/**
 * @file pcf85063.c
 * @brief PCF85063A RTC driver.
 *
 * IMPORTANT HARDWARE QUIRK (measured on this board): the PCF85063A does NOT
 * honour auto-incrementing bulk reads or writes.  A single multi-byte I2C
 * transaction returns / writes garbled bytes, so every register is accessed by
 * its own one-byte address + one-byte data transaction (see pcf85063_get /
 * pcf85063_set).  Values are BCD, and the seconds register carries an
 * oscillator-stop flag in bit 7 which is the only way to tell "never set" from
 * "set to 00:00:00".
 *
 * NOTE: the seconds counter on the unit fitted to this board is faulty - it
 * freezes while the hours register rolls once per real second.  The system
 * clock (clock_service) therefore keeps the authoritative, ticking time; this
 * driver is only the power-loss persistence layer.
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

/* Sakamoto's algorithm.  Self-contained so this driver stays in the platform
 * layer (it must not reach up into services for a weekday). */
static int day_of_week(int y, int m, int d)
{
    static const int kOff[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + kOff[m - 1] + d) % 7;
}

esp_err_t pcf85063_init(void)
{
    if (s_dev != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2c_bus_add_device(BOARD_PCF85063_I2C_ADDR, 100 * 1000, &s_dev),
                        TAG, "registering the RTC on the shared bus failed");

    /* Probe by reading the seconds register; a missing chip answers NACK and
     * this is where we find out, rather than on the first page that needs the
     * time.  Same two-step read as pcf85063_get (see note there). */
    uint8_t reg = REG_SECONDS;
    uint8_t seconds = 0;
    esp_err_t err = i2c_master_transmit(s_dev, &reg, 1, I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        err = i2c_master_receive(s_dev, &seconds, 1, I2C_TIMEOUT_MS);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no PCF85063 at 0x%02X (%s)", (unsigned)BOARD_PCF85063_I2C_ADDR,
                 esp_err_to_name(err));
        s_dev = NULL;
        return err;
    }

    /* CONTROL_1 = 0 : normal mode (not STOP), 24-hour, no correction pulse.
     * Written as its own single-byte transaction (this part does not honour
     * auto-incrementing bulk writes). */
    {
        const uint8_t c1[2] = {REG_CONTROL_1, 0x00};
        ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, c1, sizeof(c1), I2C_TIMEOUT_MS),
                            TAG, "CONTROL_1 write failed");
    }

    /* If the oscillator had stopped (power loss / first boot), the OS flag in
     * the Seconds register (bit 7) is set.  Clearing only STOP (Control_1) does
     * NOT restart the 32.768 kHz crystal: the chip then keeps its seconds
     * frozen and the higher-order fields roll at the wrong rate (the hours
     * register ticks once per real second).  Clear the OS flag so the
     * oscillator actually runs and the seconds advance at 1 Hz. */
    if (seconds & 0x80) {
        const uint8_t clr[2] = {REG_SECONDS, (uint8_t)(seconds & 0x7F)};
        i2c_master_transmit(s_dev, clr, sizeof(clr), I2C_TIMEOUT_MS);
        ESP_LOGW(TAG, "oscillator was stopped; OS flag cleared, crystal restarted");
    }

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

    /* Read each register with its own single-byte transaction: this part does
     * not honour auto-incrementing bulk reads (a multi-byte transaction returns
     * garbled bytes).  The wall-clock is laid out at 0x02..0x08 (seconds,
     * minutes, hours, days, weekdays, months, years). */
    uint8_t raw[7] = {0};
    static const uint8_t kRegs[7] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    for (int i = 0; i < 7; ++i) {
        uint8_t a = kRegs[i];
        esp_err_t e = i2c_master_transmit(s_dev, &a, 1, I2C_TIMEOUT_MS);
        if (e == ESP_OK) e = i2c_master_receive(s_dev, &raw[i], 1, I2C_TIMEOUT_MS);
    }
    s_valid = (raw[0] & 0x80) == 0;

    /* Map the BCD wall-clock registers straight onto struct tm.  No mktime()
     * round-trip: the RTC already holds a normalised calendar time, and going
     * through the C library's timezone machinery would only introduce an
     * offset we have no basis for.  The weekday is computed from the date
     * locally. */
    *out = (struct tm){0};
    out->tm_sec  = bcd2bin(raw[0] & 0x7F);
    out->tm_min  = bcd2bin(raw[1] & 0x7F);
    out->tm_hour = bcd2bin(raw[2] & 0x3F);   /* bit 6 selects 12 h; masked off */
    out->tm_mday = bcd2bin(raw[3] & 0x3F);
    out->tm_mon  = bcd2bin(raw[5] & 0x1F) - 1;   /* struct tm months are 0-based */
    out->tm_year = bcd2bin(raw[6]) + 100;        /* chip stores 2000-based years */
    out->tm_wday = day_of_week(out->tm_year + 1900, out->tm_mon + 1, out->tm_mday);
    out->tm_isdst = 0;
    return ESP_OK;
}

esp_err_t pcf85063_set(const struct tm *in)
{
    if (s_dev == NULL || in == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Write the BCD fields directly from the input calendar time.  No
     * mktime()/localtime_r() round-trip: it only injected a timezone offset
     * and (on read) a per-second hour drift.  Each register is written with its
     * OWN single-byte transaction - this part does not honour the auto-
     * incrementing bulk write (the same quirk that forces individual reads),
     * so a single 8-byte transmit would silently fail to land the time. */
    const uint8_t vals[7] = {
        (uint8_t)(bin2bcd((uint8_t)in->tm_sec) & 0x7F),   /* clear OS flag  */
        bin2bcd((uint8_t)in->tm_min),
        bin2bcd((uint8_t)in->tm_hour),
        bin2bcd((uint8_t)in->tm_mday),
        bin2bcd((uint8_t)(day_of_week(in->tm_year + 1900, in->tm_mon + 1,
                                      in->tm_mday) + 1)),
        bin2bcd((uint8_t)(in->tm_mon + 1)),
        bin2bcd((uint8_t)(in->tm_year - 100)),
    };
    for (int i = 0; i < 7; ++i) {
        const uint8_t b[2] = { (uint8_t)(REG_SECONDS + i), vals[i] };
        esp_err_t e = i2c_master_transmit(s_dev, b, sizeof(b), I2C_TIMEOUT_MS);
        if (e != ESP_OK) {
            return e;
        }
    }
    s_valid = true;
    return ESP_OK;
}

bool pcf85063_time_valid(void)
{
    return s_valid;
}
