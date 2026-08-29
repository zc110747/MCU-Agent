/**
  ******************************************************************************
  * @file    bsp_rtc.h
  * @brief   Internal RTC (STM32F429) driver wrapper.
  *
  *  Clock source: LSE (32.768 kHz); if LSE is not ready it falls back to LSI.
  *  On first power-on (backup register flag unset) a default 2026-01-01
  *  00:00:00 is loaded and the flag is set, so a warm reset never overwrites
  *  the running clock.  Time is kept in binary format (not BCD).
  ******************************************************************************
  */
#ifndef BSP_RTC_H
#define BSP_RTC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the internal RTC.
 * Returns 0 on success, -1 on failure. */
int  BSP_RTC_Init(void);

/* Read current date/time as binary values.
 *   yr: 2000..2100   mon: 1..12   day: 1..31
 *   hh: 0..23         mm: 0..59    ss: 0..59
 * Any pointer may be NULL to skip that field. */
void BSP_RTC_Get(int *yr, int *mon, int *day, int *hh, int *mm, int *ss);

/* Write date/time (binary). Validates ranges and leap-year day count.
 * Returns 0 on success, -1 if any argument is out of range. */
int  BSP_RTC_Set(int yr, int mon, int day, int hh, int mm, int ss);

/* 1 if the RTC has been initialized and a valid time loaded at least once. */
int  BSP_RTC_IsSet(void);

/* Alarm setting, persisted in backup registers (kept with VBAT).
 *   hh: 0..23   mm: 0..59   on: 0/1
 * Get returns the stored values (0/0/0 if never set). Set validates and
 * stores them; returns 0 on success, -1 on out-of-range. */
int  BSP_RTC_Alarm_Get(int *hh, int *mm, int *on);
int  BSP_RTC_Alarm_Set(int hh, int mm, int on);

/* Alarm persistence to the AT24C02 EEPROM (I2C2).
 *   LoadFromEEPROM: read the stored alarm; if the EEPROM is empty / erased
 *     (no valid magic) default to the CURRENT RTC time with the alarm OFF,
 *     then write the resolved values into the RTC backup registers so the
 *     alarm is armed at every boot.  Call once, right after BSP_RTC_Init().
 *   Persist: write hh/mm/on to EEPROM ONLY when the value changed (change
 *     detection avoids burning write cycles on repeated identical presses).
 *     Returns 0 if unchanged, >0 if written, -1 on I2C error.
 * The RAM mirror g_rtc_alarm_eeprom holds [magic, hh, mm, on] for SWD dumps. */
void BSP_RTC_Alarm_LoadFromEEPROM(void);
int  BSP_RTC_Alarm_Persist(int hh, int mm, int on);

/* RAM mirror of the EEPROM alarm record: [magic, hh, mm, on]. */
extern uint8_t g_rtc_alarm_eeprom[4];

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */
