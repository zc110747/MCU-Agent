/**
  ******************************************************************************
  * @file    drv_rtc.h
  * @brief   On-chip RTC calendar - wall clock for the LVGL UI.
  ******************************************************************************
  */
#ifndef _DRV_RTC_H
#define _DRV_RTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
  * @brief  Which low-speed oscillator ended up clocking the RTC.
  */
typedef enum
{
    RTC_CLK_NONE = 0,   /*!< RTC not running                                   */
    RTC_CLK_LSE,        /*!< 32.768 kHz crystal - accurate                     */
    RTC_CLK_LSI         /*!< ~32 kHz internal RC - +/-5%, drifts several       */
                        /*   minutes per day, fine for a demo                  */
} rtc_clk_src_t;

/**
  * @brief  Plain calendar value, no BCD, no HAL types.
  */
typedef struct
{
    uint16_t year;      /*!< 2000 .. 2099        */
    uint8_t  month;     /*!< 1 .. 12             */
    uint8_t  day;       /*!< 1 .. 31             */
    uint8_t  weekday;   /*!< 1 = Monday .. 7 = Sunday (same as RTC_WEEKDAY_x) */
    uint8_t  hour;      /*!< 0 .. 23             */
    uint8_t  minute;    /*!< 0 .. 59             */
    uint8_t  second;    /*!< 0 .. 59             */
} rtc_datetime_t;

/**
  * @brief  Bring up the RTC.
  *
  * Tries the 32.768 kHz LSE crystal first and falls back to the internal LSI
  * if it does not start within ~1 s.  On the very first power-up (or whenever
  * the backup domain lost its content) the calendar is seeded with the
  * firmware build time, so the clock is roughly right straight after flashing.
  *
  * @retval RT_OK   RTC running, calendar valid
  * @retval RT_FAIL no low-speed clock or HAL init failed
  */
GlobalType_t drv_rtc_init(void);

/**
  * @brief  Read the calendar.
  * @note   Time is read before date on purpose - the shadow registers stay
  *         frozen between the two reads, which is what keeps them coherent.
  */
GlobalType_t drv_rtc_get(rtc_datetime_t *dt);

/**
  * @brief  Write the calendar.  @p weekday may be 0: it is then derived from
  *         the date.
  */
GlobalType_t drv_rtc_set(const rtc_datetime_t *dt);

/** @brief  Which oscillator the RTC runs on (for the boot log / UI). */
rtc_clk_src_t drv_rtc_clock_source(void);

/** @brief  1 when drv_rtc_init() succeeded. */
uint8_t drv_rtc_is_ready(void);

/** @brief  1 when the calendar was seeded this boot (backup domain was lost). */
uint8_t drv_rtc_was_reset(void);

/** @brief  UTF-8 "星期一" .. "星期日"; "----" when @p weekday is out of range. */
const char *drv_rtc_weekday_cn(uint8_t weekday);

/** @brief  Monday .. Sunday, ASCII, for the UART log. */
const char *drv_rtc_weekday_en(uint8_t weekday);

/** @brief  1 = Monday .. 7 = Sunday for an arbitrary Gregorian date. */
uint8_t drv_rtc_weekday_of(uint16_t year, uint8_t month, uint8_t day);

#ifdef __cplusplus
}
#endif

#endif /* _DRV_RTC_H */
