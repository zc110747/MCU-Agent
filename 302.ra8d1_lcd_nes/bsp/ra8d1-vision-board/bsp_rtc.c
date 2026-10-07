/**
 * @file bsp_rtc.c
 * @brief Hardware RTC (R_RTC @ 0x40202000) on the 32.768 kHz sub-clock
 *
 * Project 301 has no FSP r_rtc module in its vendored FSP tree, so this file
 * programs the RTC registers directly.  That is deliberate and matches the
 * rest of bsp/ (bsp_lcd.c, bsp_touch.c drove their peripherals by hand too).
 *
 * Clocking
 * --------
 * The board fits a 32.768 kHz crystal (Y2) on XCIN/XCOUT with its load caps,
 * and cfg/bsp_cfg.h already has BSP_CLOCK_CFG_SUBCLOCK_POPULATED = 1, so
 * bsp_clock_init() -> bsp_sosc_init() starts and stabilises the sub-clock
 * before main() runs.  We therefore only have to tell the RTC to count from
 * the sub-clock (RCR4.RCKSEL = 1); the oscillator is already alive.  If that
 * ever stops being true (board rework, config change) the driver detects it:
 * bsp_rtc_init() checks SOSCCR.SOSTP and, if the oscillator is stopped,
 * starts it and waits one stabilization period itself.
 *
 * Register sequence (from the RA8D1 User's Manual RTC chapter)
 * -----------------------------------------------------------
 *   1. PRCR unlock (0xA502) - the RTC control registers live in the CGC
 *      protection window.
 *   2. RCR4.RCKSEL = 0 -> sub-clock (32.768 kHz crystal) count source.
 *      NOTE: RCKSEL is the opposite of what the name suggests on this part:
 *      0 selects the sub-clock, 1 selects the LOCO (low-speed on-chip RC
 *      oscillator).  Getting it backwards makes the RTC run off the untrimmed
 *      LOCO and lose minutes per minute - see the Phase 7 work log.
 *   3. RCR2.RESET = 1 then 0 -> reset the prescaler so the first second is a
 *      full second (also required before writing the counters).
 *   4. RCR2.HR24 = 1 -> 24-hour mode.
 *   5. Write the seven BCD calendar counters.
 *   6. RCR2.START = 1 -> the counter runs.
 *   7. PRCR lock.
 *
 * The counters are BCD (one nibble per decimal digit), which is why every
 * accessor goes through to_bcd()/from_bcd().
 *
 * @note This is only correct while the RTC clock keeps running.  A cold reset
 *       reloads the default time below; a warm reset (or VBATT backup with
 *       VBTBER.VBAE set) keeps counting.  The board has no battery, so a
 *       power cycle always restarts from the default.
 */
#include "bsp_rtc.h"

#include "bsp_api.h"

/* RTC control registers are inside the CGC/LPM protection window. */
#define BSP_RTC_PRCR_KEY     (0xA500U)
#define BSP_RTC_PRCR_UNLOCK  (BSP_RTC_PRCR_KEY | 0x03U)
#define BSP_RTC_PRCR_LOCK    (BSP_RTC_PRCR_KEY)

/* Default time loaded on a cold reset: 2026-01-01 00:00:00, Thursday.
   The RTC has no NTP, so this is just a sane anchor the application can
   overwrite over the console. */
#define BSP_RTC_DEFAULT_YEAR  (2026U)
#define BSP_RTC_DEFAULT_MON   (1U)
#define BSP_RTC_DEFAULT_MDAY  (1U)
#define BSP_RTC_DEFAULT_WDAY  (4U)   /* 0=Sunday */

/* --- the RA8D1 sub-clock settling time is 1s (bsp_cfg.h). --- */
#define BSP_RTC_SUBCLOCK_STABILIZE_MS  (1000U)

static bool s_rtc_running = false;

/* ------------------------------------------------------------------ BCD --- */

static uint8_t to_bcd (uint8_t v)
{
    return (uint8_t) (((v / 10U) << 4) | (v % 10U));
}

static uint8_t from_bcd (uint8_t v)
{
    return (uint8_t) (((v >> 4) * 10U) + (v & 0x0FU));
}

/* -------------------------------------------------------------- helpers --- */

static void prcr_unlock (void)
{
    R_SYSTEM->PRCR = (uint16_t) BSP_RTC_PRCR_UNLOCK;
}

static void prcr_lock (void)
{
    R_SYSTEM->PRCR = (uint16_t) BSP_RTC_PRCR_LOCK;
}

/* Start the sub-clock oscillator if it is not already running.
   Normally bsp_clock_init() has done this; the check keeps the driver honest
   if that ever changes. */
static void subclock_ensure_running (void)
{
    if (1U == R_SYSTEM->SOSCCR_b.SOSTP)
    {
        R_SYSTEM->SOSCCR_b.SOSTP = 0U;

        /* Manual: do not use the sub-clock until the stabilization time has
           elapsed after clearing SOSTP. 1s is the value bsp_cfg.h uses. */
        R_BSP_SoftwareDelay(BSP_RTC_SUBCLOCK_STABILIZE_MS, BSP_DELAY_UNITS_MILLISECONDS);
    }
}

/* Write the seven calendar counters.  Caller must have already put the
   prescaler in reset (RCR2.RESET = 1).  The last two year digits are all the
   RTC stores; the century lives in BSP_RTC_DEFAULT_YEAR. */
static void write_counters (const bsp_rtc_time_t * t)
{
    R_RTC->RYRCNT  = to_bcd((uint8_t) (t->year % 100U));
    R_RTC->RMONCNT = to_bcd(t->mon);
    R_RTC->RDAYCNT = to_bcd(t->mday);
    R_RTC->RWKCNT  = (uint8_t) (t->wday & 0x07U);
    R_RTC->RHRCNT  = to_bcd(t->hour);
    R_RTC->RMINCNT = to_bcd(t->min);
    R_RTC->RSECCNT = to_bcd(t->sec);
}

/* --------------------------------------------------------------- public --- */

bool bsp_rtc_init (const bsp_rtc_time_t * init)
{
    static const bsp_rtc_time_t default_time = {
        .year = BSP_RTC_DEFAULT_YEAR,
        .mon  = BSP_RTC_DEFAULT_MON,
        .mday = BSP_RTC_DEFAULT_MDAY,
        .wday = BSP_RTC_DEFAULT_WDAY,
        .hour = 0U,
        .min  = 0U,
        .sec  = 0U,
    };

    subclock_ensure_running();

    prcr_unlock();

    /* Count source = sub-clock (32.768 kHz crystal).  RCKSEL=0 -> sub-clock,
       RCKSEL=1 -> LOCO; the crystal is the accurate source. */
    R_RTC->RCR4_b.RCKSEL = 0U;

    /* Stop + software-reset the prescaler before touching the counters. */
    R_RTC->RCR2 = 0x00U;
    R_RTC->RCR2_b.RESET = 1U;
    R_RTC->RCR2_b.RESET = 0U;

    /* 24-hour mode (keep RESET/START clear for now). */
    R_RTC->RCR2_b.HR24 = 1U;

    write_counters((NULL == init) ? &default_time : init);

    /* Run. */
    R_RTC->RCR2_b.START = 1U;

    prcr_lock();

    s_rtc_running = true;
    return true;
}

bool bsp_rtc_get (bsp_rtc_time_t * out)
{
    if (!s_rtc_running || (NULL == out))
    {
        return false;
    }

    /* The counters are read without a carry latch; a roll-over between two
       reads is possible in principle but only for a fraction of a microsecond
       per second, and this is a display clock, so read the seconds twice and
       retry if it changed under us. */
    uint8_t sec_first = R_RTC->RSECCNT;
    out->sec  = from_bcd(sec_first);
    out->min  = from_bcd(R_RTC->RMINCNT);
    out->hour = from_bcd(R_RTC->RHRCNT);
    out->wday = R_RTC->RWKCNT_b.DAYW;
    out->mday = from_bcd(R_RTC->RDAYCNT);
    out->mon  = from_bcd(R_RTC->RMONCNT);
    uint8_t yy = (uint8_t) from_bcd(R_RTC->RYRCNT);
    out->year = (uint16_t) ((BSP_RTC_DEFAULT_YEAR / 100U) * 100U + yy);
    uint8_t sec_last = R_RTC->RSECCNT;

    if (sec_first != sec_last)
    {
        /* Carry happened mid-read: redo once. */
        out->sec  = from_bcd(sec_last);
        out->min  = from_bcd(R_RTC->RMINCNT);
        out->hour = from_bcd(R_RTC->RHRCNT);
        out->wday = R_RTC->RWKCNT_b.DAYW;
        out->mday = from_bcd(R_RTC->RDAYCNT);
        out->mon  = from_bcd(R_RTC->RMONCNT);
        yy        = (uint8_t) from_bcd(R_RTC->RYRCNT);
        out->year = (uint16_t) ((BSP_RTC_DEFAULT_YEAR / 100U) * 100U + yy);
    }

    return true;
}

bool bsp_rtc_set (const bsp_rtc_time_t * t)
{
    if (NULL == t)
    {
        return false;
    }

    prcr_unlock();

    /* Pause counting, reset the prescaler, reload, restart. */
    R_RTC->RCR2 = 0x00U;
    R_RTC->RCR2_b.RESET = 1U;
    R_RTC->RCR2_b.RESET = 0U;
    R_RTC->RCR2_b.HR24  = 1U;

    write_counters(t);

    R_RTC->RCR2_b.START = 1U;

    prcr_lock();

    s_rtc_running = true;
    return true;
}

bool bsp_rtc_is_running (void)
{
    return s_rtc_running;
}
