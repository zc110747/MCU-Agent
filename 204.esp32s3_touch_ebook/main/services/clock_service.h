/**
 * @file clock_service.h
 * @brief Time for the UI: RTC access, formatting and calendar arithmetic.
 *
 * Sits between the pages and the PCF85063 so that no page has to know about
 * BCD registers, the oscillator-stop flag or which I2C address the chip is on.
 * The calendar helpers live here too because "February in a leap year" is a
 * data question, not a drawing question, and the Clock, Calendar and Home
 * header all need the same answer.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace services {

struct TimeParts {
    int year;    /* full year, e.g. 2026 */
    int month;   /* 1..12               */
    int day;     /* 1..31               */
    int hour;    /* 0..23               */
    int minute;  /* 0..59               */
    int second;  /* 0..59               */
    int weekday; /* 0 = Sunday          */
};

/**
 * @brief Bring up the RTC.
 *
 * If the chip reports that its oscillator stopped (fresh battery, first boot)
 * the time is seeded from the build timestamp so the clock is at least
 * plausible and monotonic, and the fact is logged.
 */
esp_err_t clock_init();

/** @brief False when the RTC has never been set. */
bool clock_valid();

/** @brief Current time. Falls back to the build time when no RTC is present. */
void clock_now(TimeParts *out);

/** @brief Apply a time to the RTC. */
esp_err_t clock_set(const TimeParts *in);

/** @brief "HH:MM" or "HH:MM:SS", 24-hour or 12-hour with an am/pm suffix. */
void clock_format_time(char *buf, size_t len, const TimeParts *t, bool with_seconds, bool h24);

/** @brief "YYYY-MM-DD". */
void clock_format_date(char *buf, size_t len, const TimeParts *t);

/** @brief Short weekday name, e.g. "Mon". */
const char *clock_weekday_name(int weekday);

/** @brief Short month name, e.g. "Jan". */
const char *clock_month_name(int month);

/* ---- calendar arithmetic ------------------------------------------------ */

bool clock_is_leap_year(int year);

/** @brief Days in @p month (1..12) of @p year. */
int clock_days_in_month(int year, int month);

/**
 * @brief Day of week for a date, 0 = Sunday.
 *
 * Uses Sakamoto's algorithm: it is branch-free, valid for any Gregorian date,
 * and short enough to be obviously correct.
 */
int clock_day_of_week(int year, int month, int day);

/** @brief Advance/rewind a month, rolling the year over as needed. */
void clock_add_months(int *year, int *month, int delta);

}  // namespace services
