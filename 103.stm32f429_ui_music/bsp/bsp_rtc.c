/**
  ******************************************************************************
  * @file    bsp_rtc.c
  * @brief   Internal RTC (STM32F429) driver wrapper.
  *
  *  See bsp_rtc.h for the contract.  HAL_RTC_MspInit is implemented here
  *  (the weak default in the HAL is overridden) because the project does not
  *  ship the HAL MSP template.
  ******************************************************************************
  */
#include "bsp_rtc.h"
#include "stm32f4xx_hal.h"
#include "log.h"
#include "bsp_eeprom_24c02.h"
#include <stdbool.h>
#include <string.h>

/* Default time loaded on first power-on. */
#define RTC_DEF_YEAR 2026
#define RTC_DEF_MON  1
#define RTC_DEF_DAY  1
#define RTC_DEF_HOUR 0
#define RTC_DEF_MIN  0
#define RTC_DEF_SEC  0

/* Backup register used as the "RTC already initialized" flag. */
#define RTC_INIT_FLAG 0x55AAU

static RTC_HandleTypeDef s_rtc;
static bool             s_rtc_ok = false;

static bool rtc_is_leap(int yr)
{
    if ((yr % 4) != 0) return false;
    if ((yr % 100) != 0) return true;
    return ((yr % 400) == 0);
}

static int rtc_days_in_month(int yr, int mon)
{
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (mon < 1 || mon > 12) return 0;
    if (mon == 2 && rtc_is_leap(yr)) return 29;
    return dim[mon - 1];
}

void HAL_RTC_MspInit(RTC_HandleTypeDef *hrtc)
{
    RCC_OscInitTypeDef         osc = {0};
    RCC_PeriphCLKInitTypeDef   clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    /* LSE first, LSI as fallback. */
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState       = RCC_LSE_ON;
    osc.PLL.PLLState   = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
        osc.LSEState       = RCC_LSE_OFF;
        osc.LSIState       = RCC_LSI_ON;
        if (HAL_RCC_OscConfig(&osc) == HAL_OK)
        {
            PRINT_LOG("[RTC ] LSE unavailable, fallback to LSI\r\n");
        }
        else
        {
            PRINT_LOG("[RTC ] LSE/LSI both failed!\r\n");
        }
    }
    else
    {
        PRINT_LOG("[RTC ] LSE started\r\n");
    }

    clk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET)
    {
        clk.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    }
    else
    {
        clk.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    }
    HAL_RCCEx_PeriphCLKConfig(&clk);

    __HAL_RCC_RTC_ENABLE();
    (void)hrtc;
}

int BSP_RTC_Init(void)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    s_rtc.Instance          = RTC;
    s_rtc.Init.HourFormat   = RTC_HOURFORMAT_24;
    s_rtc.Init.AsynchPrediv = 127;     /* 32.768 kHz / 128 = 256 Hz */
    s_rtc.Init.SynchPrediv  = 255;     /* 256 Hz / 256 = 1 Hz */
    s_rtc.Init.OutPut       = RTC_OUTPUT_DISABLE;
    s_rtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    s_rtc.Init.OutPutType   = RTC_OUTPUT_TYPE_OPENDRAIN;

    if (HAL_RTC_Init(&s_rtc) != HAL_OK)
    {
        PRINT_LOG("[RTC ] HAL_RTC_Init failed\r\n");
        return -1;
    }
    s_rtc_ok = true;

    if (HAL_RTCEx_BKUPRead(&s_rtc, RTC_BKP_DR0) != RTC_INIT_FLAG)
    {
        d.Year    = (uint8_t)(RTC_DEF_YEAR - 2000);
        d.Month   = (uint8_t)RTC_DEF_MON;
        d.Date    = (uint8_t)RTC_DEF_DAY;
        d.WeekDay = RTC_WEEKDAY_MONDAY;
        t.Hours   = (uint8_t)RTC_DEF_HOUR;
        t.Minutes = (uint8_t)RTC_DEF_MIN;
        t.Seconds = (uint8_t)RTC_DEF_SEC;
        t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        t.StoreOperation = RTC_STOREOPERATION_RESET;
        HAL_RTC_SetDate(&s_rtc, &d, RTC_FORMAT_BIN);
        HAL_RTC_SetTime(&s_rtc, &t, RTC_FORMAT_BIN);
        HAL_RTCEx_BKUPWrite(&s_rtc, RTC_BKP_DR0, RTC_INIT_FLAG);
        PRINT_LOG("[RTC ] default time loaded 2026-01-01 00:00:00\r\n");
    }
    else
    {
        PRINT_LOG("[RTC ] time retained from backup\r\n");
    }
    return 0;
}

void BSP_RTC_Get(int *yr, int *mon, int *day, int *hh, int *mm, int *ss)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    if (!s_rtc_ok) return;

    /* Read time first, then date: the date is latched when the time register
     * is read, so this ordering avoids a one-second skew at a rollover. */
    HAL_RTC_GetTime(&s_rtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&s_rtc, &d, RTC_FORMAT_BIN);

    if (yr)  *yr  = (int)d.Year + 2000;
    if (mon) *mon = (int)d.Month;
    if (day) *day = (int)d.Date;
    if (hh)  *hh  = (int)t.Hours;
    if (mm)  *mm  = (int)t.Minutes;
    if (ss)  *ss  = (int)t.Seconds;
}

int BSP_RTC_Set(int yr, int mon, int day, int hh, int mm, int ss)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    if (!s_rtc_ok) return -1;
    if (yr < 2000 || yr > 2100) return -1;
    if (mon < 1 || mon > 12) return -1;
    if (day < 1 || day > rtc_days_in_month(yr, mon)) return -1;
    if (hh < 0 || hh > 23) return -1;
    if (mm < 0 || mm > 59) return -1;
    if (ss < 0 || ss > 59) return -1;

    d.Year    = (uint8_t)(yr - 2000);
    d.Month   = (uint8_t)mon;
    d.Date    = (uint8_t)day;
    d.WeekDay = RTC_WEEKDAY_MONDAY;
    t.Hours   = (uint8_t)hh;
    t.Minutes = (uint8_t)mm;
    t.Seconds = (uint8_t)ss;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;

    if (HAL_RTC_SetDate(&s_rtc, &d, RTC_FORMAT_BIN) != HAL_OK) return -1;
    if (HAL_RTC_SetTime(&s_rtc, &t, RTC_FORMAT_BIN) != HAL_OK) return -1;
    return 0;
}

/* Backup registers for the alarm (retained as long as VBAT is present).
 * NOTE: HAL_RTCEx_BKUPWrite/Read take a REGISTER INDEX (0..19), NOT the
 * register-name mask.  Use the RTC_BKP_DRx index macros.  (Passing
 * RTC_BKP1R would resolve to 0xFFFFFFFFUL and write to a garbage address.) */
#define RTC_ALARM_HH  RTC_BKP_DR1
#define RTC_ALARM_MM  RTC_BKP_DR2
#define RTC_ALARM_ON  RTC_BKP_DR3

int BSP_RTC_Alarm_Get(int *hh, int *mm, int *on)
{
    if (hh)  *hh  = (int)HAL_RTCEx_BKUPRead(&s_rtc, RTC_ALARM_HH);
    if (mm)  *mm  = (int)HAL_RTCEx_BKUPRead(&s_rtc, RTC_ALARM_MM);
    if (on)  *on  = (HAL_RTCEx_BKUPRead(&s_rtc, RTC_ALARM_ON) == 1U) ? 1 : 0;
    return 0;
}

int BSP_RTC_Alarm_Set(int hh, int mm, int on)
{
    if (!s_rtc_ok) return -1;
    if (hh < 0 || hh > 23) return -1;
    if (mm < 0 || mm > 59) return -1;

    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    HAL_RTCEx_BKUPWrite(&s_rtc, RTC_ALARM_HH, (uint32_t)hh);
    HAL_RTCEx_BKUPWrite(&s_rtc, RTC_ALARM_MM, (uint32_t)mm);
    HAL_RTCEx_BKUPWrite(&s_rtc, RTC_ALARM_ON, (on != 0) ? 1U : 0U);
    return 0;
}

/* ---- EEPROM alarm persistence (AT24C02, I2C2) ---- */
#define RTC_ALARM_EEP_OFF   0U
#define RTC_ALARM_EEP_MAGIC 0xA5U

/* RAM mirror: [magic, hh, mm, on]. Inspect with SWD (mdw, 4-byte aligned). */
uint8_t g_rtc_alarm_eeprom[4] = {0};

void BSP_RTC_Alarm_LoadFromEEPROM(void)
{
    uint8_t buf[4];
    int hh = 0, mm = 0, on = 0;

    g_rtc_alarm_eeprom[0] = RTC_ALARM_EEP_MAGIC;

    if (EEPROM24_Read(RTC_ALARM_EEP_OFF, buf, 4) == 0 &&
        buf[0] == RTC_ALARM_EEP_MAGIC)
    {
        hh = (int)buf[1];
        mm = (int)buf[2];
        on = (buf[3] != 0U) ? 1 : 0;
        PRINT_LOG("[RTC ] alarm loaded from EEPROM %02d:%02d %s\r\n",
                  hh, mm, on ? "on" : "off");
    }
    else
    {
        /* First power-on or erased EEPROM: default to the CURRENT RTC time,
         * alarm disabled. */
        BSP_RTC_Get(NULL, NULL, NULL, &hh, &mm, NULL);
        on = 0;
        PRINT_LOG("[RTC ] alarm EEPROM empty, default current %02d:%02d off\r\n",
                  hh, mm);
    }

    g_rtc_alarm_eeprom[1] = (uint8_t)hh;
    g_rtc_alarm_eeprom[2] = (uint8_t)mm;
    g_rtc_alarm_eeprom[3] = (uint8_t)on;

    (void)BSP_RTC_Alarm_Set(hh, mm, on);
}

int BSP_RTC_Alarm_Persist(int hh, int mm, int on)
{
    uint8_t cur[4];

    /* Change detection: if the stored record already equals the request,
     * skip the write cycle entirely (don't wear the EEPROM on repeated
     * identical presses of 开启 / 关闭). */
    if (EEPROM24_Read(RTC_ALARM_EEP_OFF, cur, 4) == 0 &&
        cur[0] == RTC_ALARM_EEP_MAGIC &&
        cur[1] == (uint8_t)hh && cur[2] == (uint8_t)mm &&
        cur[3] == (uint8_t)on)
    {
        return 0;
    }

    uint8_t buf[4] = { RTC_ALARM_EEP_MAGIC, (uint8_t)hh, (uint8_t)mm, (uint8_t)on };
    (void)memcpy(g_rtc_alarm_eeprom, buf, 4);
    PRINT_LOG("[RTC ] alarm persist -> EEPROM %02d:%02d %s\r\n",
              hh, mm, on ? "on" : "off");
    return EEPROM24_Write(RTC_ALARM_EEP_OFF, buf, 4);
}

int BSP_RTC_IsSet(void)
{
    return ((s_rtc_ok != false) &&
            (HAL_RTCEx_BKUPRead(&s_rtc, RTC_BKP_DR0) == RTC_INIT_FLAG)) ? 1 : 0;
}
