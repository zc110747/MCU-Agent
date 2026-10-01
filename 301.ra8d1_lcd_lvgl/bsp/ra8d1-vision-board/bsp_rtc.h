/**
 * @file bsp_rtc.h
 * @brief Hardware real-time clock (R_RTC) driven from the 32.768 kHz sub-clock
 *
 * The RA8D1 has no FSP r_rtc module in this project's FSP tree, so this driver
 * programs the RTC registers directly.  The count source is the sub-clock
 * oscillator XCIN/XCOUT, which the board populates with a 32.768 kHz crystal
 * (Y2) and its load caps, so the RTC keeps real calendar time rather than
 * reset-relative uptime.
 *
 * All accessors use the BCD calendar registers of the RTC (RSECCNT / RMINCNT /
 * RHRCNT / RWKCNT / RDAYCNT / RMONCNT / RYRCNT).  Hours are kept in 24-hour
 * mode (RCR2.HR24 = 1).
 */
#ifndef BSP_RTC_H
#define BSP_RTC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Calendar / clock fields, in natural (non-BCD) units. */
typedef struct
{
    uint16_t year; /**< Full year, e.g. 2026. */
    uint8_t  mon;  /**< Month, 1..12. */
    uint8_t  mday; /**< Day of month, 1..31. */
    uint8_t  wday; /**< Day of week, 0=Sunday .. 6=Saturday. */
    uint8_t  hour; /**< Hour, 0..23. */
    uint8_t  min;  /**< Minute, 0..59. */
    uint8_t  sec;  /**< Second, 0..59. */
} bsp_rtc_time_t;

/**
 * @brief Start the sub-clock oscillator and the RTC, then load a default time.
 *
 * Idempotent-safe to call once at boot.  If the RTC is already running (a warm
 * reset with a live backup domain), the existing time is preserved.
 *
 * @param[in] init  Time to load when the RTC is starting from a cold reset.
 *                  May be NULL, in which case a fixed build-time default is
 *                  used.  Ignored when the RTC is already running.
 * @return true on success (RTC running), false if the sub-clock never
 *         stabilised.
 */
bool bsp_rtc_init(const bsp_rtc_time_t * init);

/** @brief Read the current calendar time. Returns false if the RTC is not up. */
bool bsp_rtc_get(bsp_rtc_time_t * out);

/** @brief Set the calendar time (writes all BCD counters). */
bool bsp_rtc_set(const bsp_rtc_time_t * t);

/** @brief True once bsp_rtc_init() has started the counter. */
bool bsp_rtc_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */
