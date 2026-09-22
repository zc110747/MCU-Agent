/**
 * @file pcf85063.h
 * @brief PCF85063A real-time clock on the shared I2C bus (0x51).
 *
 * WHY THE NAMES CARRY THE CHIP PREFIX
 * -----------------------------------
 * These used to be rtc_init() / rtc_get() / rtc_set().  ESP-IDF's own
 * esp_hw_support component exports a global `rtc_init`, so the two collided -
 * and only at link time, as
 *
 *     multiple definition of `rtc_init'; first defined here
 *
 * which is a much later and more confusing place to learn that C has one flat
 * global namespace.  Naming platform entry points after the part they drive
 * removes the whole class of problem, and it also reads better: there are three
 * things on this board called "RTC" (the chip, the AXP's RTC, and the SoC's),
 * and `pcf85063_*` says which one.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Probe the RTC and log whether its oscillator flag says the time is real. */
esp_err_t pcf85063_init(void);

/** @brief Read the RTC into a broken-down time. */
esp_err_t pcf85063_get(struct tm *out);

/** @brief Write a broken-down time to the RTC. */
esp_err_t pcf85063_set(const struct tm *in);

/**
 * @brief Whether the RTC is holding a plausible time.
 *
 * False after a power loss (the chip raises its oscillator-stop flag), which is
 * the difference between showing 00:00 and showing a wrong-but-confident time.
 */
bool pcf85063_time_valid(void);

#ifdef __cplusplus
}
#endif
