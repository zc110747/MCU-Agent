#include "clock_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pcf85063.h"

#include "esp_log.h"

static const char *TAG = "clock_svc";

namespace services {

namespace {

bool s_rtc_present = false;
bool s_valid = false;

/* Used when the RTC is absent or unset.  Seeding from the build timestamp at
 * least makes the clock advance coherently and gives the logs a reference. */
struct tm build_time()
{
    static const char *kDate = __DATE__;   /* "Sep 22 2026" */
    static const char *kTime = __TIME__;   /* "13:45:07"    */

    static const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    struct tm t = {};
    char mon[4] = {0};
    int day = 1;
    int year = 2026;
    int hour = 0, min = 0, sec = 0;

    if (sscanf(kDate, "%3s %d %d", mon, &day, &year) == 3) {
        for (int i = 0; i < 12; ++i) {
            if (strcmp(mon, kMonths[i]) == 0) {
                t.tm_mon = i;
                break;
            }
        }
    }
    sscanf(kTime, "%d:%d:%d", &hour, &min, &sec);

    t.tm_mday = day;
    t.tm_year = year - 1900;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    mktime(&t);   /* normalises tm_wday / tm_yday */
    return t;
}

const struct tm &fallback()
{
    static const struct tm kBuild = build_time();
    return kBuild;
}

}  // namespace

esp_err_t clock_init()
{
    esp_err_t err = pcf85063_init();
    s_rtc_present = (err == ESP_OK);

    if (!s_rtc_present) {
        s_valid = false;
        ESP_LOGW(TAG, "no RTC; the clock will use the build timestamp");
        return err;
    }

    s_valid = pcf85063_time_valid();
    if (!s_valid) {
        /* Seed it so the device shows a moving clock instead of frozen zeros.
         * Logged as a warning because it is wrong by the time since the build. */
        struct tm seed = fallback();
        if (pcf85063_set(&seed) == ESP_OK) {
            s_valid = true;
            ESP_LOGW(TAG, "RTC had never been set; seeded from the build timestamp");
        }
    }
    return ESP_OK;
}

bool clock_valid()
{
    return s_valid;
}

void clock_now(TimeParts *out)
{
    if (out == nullptr) {
        return;
    }

    struct tm t = {};
    if (s_rtc_present && pcf85063_get(&t) == ESP_OK) {
        s_valid = pcf85063_time_valid();
    } else {
        t = fallback();
    }

    out->year = t.tm_year + 1900;
    out->month = t.tm_mon + 1;
    out->day = t.tm_mday;
    out->hour = t.tm_hour;
    out->minute = t.tm_min;
    out->second = t.tm_sec;
    out->weekday = t.tm_wday;
}

esp_err_t clock_set(const TimeParts *in)
{
    if (in == nullptr || !s_rtc_present) {
        return ESP_ERR_INVALID_STATE;
    }
    struct tm t = {};
    t.tm_year = in->year - 1900;
    t.tm_mon = in->month - 1;
    t.tm_mday = in->day;
    t.tm_hour = in->hour;
    t.tm_min = in->minute;
    t.tm_sec = in->second;
    t.tm_isdst = -1;
    esp_err_t err = pcf85063_set(&t);
    if (err == ESP_OK) {
        s_valid = true;
    }
    return err;
}

void clock_format_time(char *buf, size_t len, const TimeParts *t, bool with_seconds, bool h24)
{
    if (buf == nullptr || len == 0 || t == nullptr) {
        return;
    }

    if (h24) {
        if (with_seconds) {
            snprintf(buf, len, "%02d:%02d:%02d", t->hour, t->minute, t->second);
        } else {
            snprintf(buf, len, "%02d:%02d", t->hour, t->minute);
        }
        return;
    }

    /* 12-hour clock: 0 -> 12, 13 -> 1, and midday/afternoon is "pm". */
    int hour = t->hour % 12;
    if (hour == 0) {
        hour = 12;
    }
    const char *suffix = (t->hour < 12) ? "am" : "pm";

    /* Two separate calls rather than one snprintf with a chosen format string:
     * the single-call version has to pass the seconds argument unconditionally,
     * and when the format omits %02d for it the string argument lines up with
     * an int.  That compiles to a format warning and, at run time, to printing
     * whatever integer happened to sit next on the stack as a suffix. */
    if (with_seconds) {
        snprintf(buf, len, "%d:%02d:%02d %s", hour, t->minute, t->second, suffix);
    } else {
        snprintf(buf, len, "%d:%02d %s", hour, t->minute, suffix);
    }
}

void clock_format_date(char *buf, size_t len, const TimeParts *t)
{
    if (buf == nullptr || len == 0 || t == nullptr) {
        return;
    }
    snprintf(buf, len, "%04d-%02d-%02d", t->year, t->month, t->day);
}

const char *clock_weekday_name(int weekday)
{
    static const char *kNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (weekday < 0 || weekday > 6) {
        return "---";
    }
    return kNames[weekday];
}

const char *clock_month_name(int month)
{
    static const char *kNames[] = {"---", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (month < 1 || month > 12) {
        return "---";
    }
    return kNames[month];
}

bool clock_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int clock_days_in_month(int year, int month)
{
    static const int kDays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && clock_is_leap_year(year)) {
        return 29;
    }
    return kDays[month];
}

int clock_day_of_week(int year, int month, int day)
{
    /* Sakamoto: no tables, no branches, correct for the whole Gregorian
     * calendar including the 1900 and 2000 century rules. */
    static const int kOffset[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3) {
        y -= 1;
    }
    return (y + y / 4 - y / 100 + y / 400 + kOffset[month - 1] + day) % 7;
}

void clock_add_months(int *year, int *month, int delta)
{
    if (year == nullptr || month == nullptr) {
        return;
    }
    int m = *month - 1 + delta;   /* 0-based for the division to floor correctly */
    int y = *year + (m >= 0 ? m / 12 : (m - 11) / 12);
    m = m - (y - *year) * 12;

    *year = y;
    *month = m + 1;
}

}  // namespace services
