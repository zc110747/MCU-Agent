/**
  ******************************************************************************
  * @file    drv_rtc.c
  * @brief   On-chip RTC calendar.
  *
  *  Clock source
  *  ------------
  *  The RTC is clocked from LSE (32.768 kHz crystal) when one is fitted and
  *  starts, otherwise from the internal LSI RC.  LSE is tried with a short
  *  hand-rolled 1 s timeout instead of HAL_RCC_OscConfig(): the HAL path uses
  *  LSE_TIMEOUT_VALUE (5 s) and would stall the boot on every board that has
  *  no crystal.  LSI is only +/-5% accurate - good enough to show a clock,
  *  not good enough to keep time for a week.
  *
  *  Backup domain
  *  -------------
  *  BKP_DR0 holds a magic word.  If it does not read back, the calendar
  *  content is garbage (first power-up, or no VBAT cell fitted) and we seed it
  *  from the firmware build timestamp, so the displayed time is within a
  *  minute of reality right after flashing.  Boards with a VBAT battery keep
  *  running across resets and are left untouched.
  *
  *  MSP
  *  ---
  *  No HAL_RTC_MspInit() is provided on purpose.  The RTC needs its kernel
  *  clock selected *before* the peripheral is enabled and the LSE/LSI choice
  *  has to report back which one won, which does not fit the void MspInit
  *  signature.  Everything is done inline in drv_rtc_init() instead.
  ******************************************************************************
  */
#include "drv_rtc.h"
#include <string.h>

/* Written to BKP_DR0 once the calendar holds a real date. */
#define RTC_BKP_MAGIC_REG       RTC_BKP_DR0
#define RTC_BKP_MAGIC_VALUE     0x32F7C10CU

/* How long to wait for the LSE crystal before giving up on it. */
#define LSE_START_TIMEOUT_MS    1000U
#define LSI_START_TIMEOUT_MS    100U

/* Prescalers: (async + 1) * (sync + 1) must equal the input frequency. */
#define LSE_ASYNC_PREDIV        127U
#define LSE_SYNC_PREDIV         255U    /* 128 * 256 = 32768 */
#define LSI_ASYNC_PREDIV        127U
#define LSI_SYNC_PREDIV         249U    /* 128 * 250 = 32000 (LSI nominal)   */

static RTC_HandleTypeDef s_hrtc;
static rtc_clk_src_t     s_clk_src   = RTC_CLK_NONE;
static uint8_t           s_ready     = 0U;
static uint8_t           s_was_reset = 0U;

/*----------------------------------------------------------------------------
 *  Helpers
 *--------------------------------------------------------------------------*/

/**
  * @brief  Sakamoto's day-of-week algorithm, remapped to the RTC numbering
  *         (1 = Monday .. 7 = Sunday).
  */
uint8_t drv_rtc_weekday_of(uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    uint32_t y = year;
    uint32_t w;

    if ((month < 1U) || (month > 12U))
    {
        return 1U;
    }
    if (month < 3U)
    {
        y -= 1U;
    }

    w = (y + (y / 4U) - (y / 100U) + (y / 400U) + t[month - 1U] + day) % 7U;

    /* w: 0 = Sunday .. 6 = Saturday  ->  RTC: 1 = Monday .. 7 = Sunday */
    return (w == 0U) ? 7U : (uint8_t)w;
}

const char *drv_rtc_weekday_cn(uint8_t weekday)
{
    static const char *const names[7] =
    {
        "星期一", "星期二", "星期三", "星期四", "星期五", "星期六", "星期日"
    };

    if ((weekday < 1U) || (weekday > 7U))
    {
        return "----";
    }
    return names[weekday - 1U];
}

const char *drv_rtc_weekday_en(uint8_t weekday)
{
    static const char *const names[7] =
    {
        "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
        "Saturday", "Sunday"
    };

    if ((weekday < 1U) || (weekday > 7U))
    {
        return "?";
    }
    return names[weekday - 1U];
}

/**
  * @brief  Decode __DATE__ ("Mmm dd yyyy") and __TIME__ ("hh:mm:ss") into a
  *         calendar value.
  *
  * The day field of __DATE__ is space padded for days 1..9, hence the isdigit
  * style check instead of a blind two-digit parse.
  */
static void build_timestamp(rtc_datetime_t *dt)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;
    const char *t = __TIME__;
    uint8_t     i;

    dt->month = 1U;
    for (i = 0U; i < 12U; i++)
    {
        if ((months[i * 3U] == d[0]) &&
            (months[i * 3U + 1U] == d[1]) &&
            (months[i * 3U + 2U] == d[2]))
        {
            dt->month = (uint8_t)(i + 1U);
            break;
        }
    }

    dt->day = (uint8_t)((d[4] == ' ') ? (d[5] - '0')
                                      : ((d[4] - '0') * 10 + (d[5] - '0')));

    dt->year = (uint16_t)((d[7]  - '0') * 1000 + (d[8]  - '0') * 100 +
                          (d[9]  - '0') * 10   + (d[10] - '0'));

    dt->hour   = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
    dt->minute = (uint8_t)((t[3] - '0') * 10 + (t[4] - '0'));
    dt->second = (uint8_t)((t[6] - '0') * 10 + (t[7] - '0'));

    dt->weekday = drv_rtc_weekday_of(dt->year, dt->month, dt->day);
}

/**
  * @brief  Start LSE, then LSI, and report which one is usable.
  *
  * @retval RTC_CLK_LSE / RTC_CLK_LSI / RTC_CLK_NONE
  */
static rtc_clk_src_t low_speed_clock_start(void)
{
    uint32_t tick;

    /* BDCR and the backup registers are write protected out of reset. */
    HAL_PWR_EnableBkUpAccess();

    /* Already running (warm reset with VBAT)?  Do not touch it. */
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET)
    {
        return RTC_CLK_LSE;
    }

    __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
    tick = HAL_GetTick();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET)
    {
        if ((HAL_GetTick() - tick) > LSE_START_TIMEOUT_MS)
        {
            /* No crystal on OSC32_IN/OUT (or it is dead) - shut it back off
             * so the oscillator driver is not left burning current. */
            __HAL_RCC_LSE_CONFIG(RCC_LSE_OFF);
            break;
        }
    }

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET)
    {
        return RTC_CLK_LSE;
    }

    __HAL_RCC_LSI_ENABLE();
    tick = HAL_GetTick();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET)
    {
        if ((HAL_GetTick() - tick) > LSI_START_TIMEOUT_MS)
        {
            return RTC_CLK_NONE;
        }
    }

    return RTC_CLK_LSI;
}

/*----------------------------------------------------------------------------
 *  Public API
 *--------------------------------------------------------------------------*/

GlobalType_t drv_rtc_init(void)
{
    RCC_PeriphCLKInitTypeDef pclk = {0};
    rtc_datetime_t           seed;

    s_ready     = 0U;
    s_was_reset = 0U;

    s_clk_src = low_speed_clock_start();
    if (s_clk_src == RTC_CLK_NONE)
    {
        return RT_FAIL;
    }

    /* Select the RTC kernel clock.  HAL_RCCEx_PeriphCLKConfig() resets the
     * backup domain internally if RTCSEL is being changed, and restores the
     * rest of BDCR (including LSEON) afterwards. */
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection    = (s_clk_src == RTC_CLK_LSE)
                                ? RCC_RTCCLKSOURCE_LSE
                                : RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK)
    {
        s_clk_src = RTC_CLK_NONE;
        return RT_FAIL;
    }

    __HAL_RCC_RTC_ENABLE();

    s_hrtc.Instance            = RTC;
    s_hrtc.Init.HourFormat     = RTC_HOURFORMAT_24;
    s_hrtc.Init.AsynchPrediv   = (s_clk_src == RTC_CLK_LSE) ? LSE_ASYNC_PREDIV
                                                            : LSI_ASYNC_PREDIV;
    s_hrtc.Init.SynchPrediv    = (s_clk_src == RTC_CLK_LSE) ? LSE_SYNC_PREDIV
                                                            : LSI_SYNC_PREDIV;
    s_hrtc.Init.OutPut         = RTC_OUTPUT_DISABLE;
    s_hrtc.Init.OutPutRemap    = RTC_OUTPUT_REMAP_NONE;
    s_hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_hrtc.Init.OutPutType     = RTC_OUTPUT_TYPE_OPENDRAIN;
#if defined(TAMP)
    s_hrtc.Init.OutPutPullUp   = RTC_OUTPUT_PULLUP_NONE;
#endif

    /* HAL_RTC_Init() only touches PRER/CR when the calendar has never been
     * initialised (INITS = 0), so a battery-backed clock survives this call. */
    if (HAL_RTC_Init(&s_hrtc) != HAL_OK)
    {
        s_clk_src = RTC_CLK_NONE;
        return RT_FAIL;
    }

    if (HAL_RTCEx_BKUPRead(&s_hrtc, RTC_BKP_MAGIC_REG) != RTC_BKP_MAGIC_VALUE)
    {
        /* Backup domain was lost - seed from the build timestamp. */
        build_timestamp(&seed);
        if (drv_rtc_set(&seed) != RT_OK)
        {
            return RT_FAIL;
        }
        HAL_RTCEx_BKUPWrite(&s_hrtc, RTC_BKP_MAGIC_REG, RTC_BKP_MAGIC_VALUE);
        s_was_reset = 1U;
    }

    s_ready = 1U;
    return RT_OK;
}

GlobalType_t drv_rtc_set(const rtc_datetime_t *dt)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    uint8_t         wd;

    if ((dt == NULL) || (s_clk_src == RTC_CLK_NONE))
    {
        return RT_FAIL;
    }
    if ((dt->month < 1U) || (dt->month > 12U) ||
        (dt->day   < 1U) || (dt->day   > 31U) ||
        (dt->hour  > 23U) || (dt->minute > 59U) || (dt->second > 59U) ||
        (dt->year  < 2000U) || (dt->year > 2099U))
    {
        return RT_FAIL;
    }

    wd = dt->weekday;
    if ((wd < 1U) || (wd > 7U))
    {
        wd = drv_rtc_weekday_of(dt->year, dt->month, dt->day);
    }

    t.Hours          = dt->hour;
    t.Minutes        = dt->minute;
    t.Seconds        = dt->second;
    t.TimeFormat     = RTC_HOURFORMAT12_AM;   /* ignored in 24 h mode */
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(&s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK)
    {
        return RT_FAIL;
    }

    d.WeekDay = wd;
    d.Month   = dt->month;
    d.Date    = dt->day;
    d.Year    = (uint8_t)(dt->year - 2000U);
    if (HAL_RTC_SetDate(&s_hrtc, &d, RTC_FORMAT_BIN) != HAL_OK)
    {
        return RT_FAIL;
    }

    return RT_OK;
}

GlobalType_t drv_rtc_get(rtc_datetime_t *dt)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    if ((dt == NULL) || (s_clk_src == RTC_CLK_NONE))
    {
        return RT_FAIL;
    }

    /* Order matters: reading TR freezes the shadow registers, reading DR
     * releases them.  Swapping these two gives a date/time that can straddle
     * midnight. */
    if (HAL_RTC_GetTime(&s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK)
    {
        return RT_FAIL;
    }
    if (HAL_RTC_GetDate(&s_hrtc, &d, RTC_FORMAT_BIN) != HAL_OK)
    {
        return RT_FAIL;
    }

    dt->hour    = t.Hours;
    dt->minute  = t.Minutes;
    dt->second  = t.Seconds;
    dt->year    = (uint16_t)(2000U + d.Year);
    dt->month   = d.Month;
    dt->day     = d.Date;
    dt->weekday = d.WeekDay;

    return RT_OK;
}

rtc_clk_src_t drv_rtc_clock_source(void)
{
    return s_clk_src;
}

uint8_t drv_rtc_is_ready(void)
{
    return s_ready;
}

uint8_t drv_rtc_was_reset(void)
{
    return s_was_reset;
}
