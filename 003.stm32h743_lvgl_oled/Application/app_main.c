/**
  ******************************************************************************
  * @file    app_main.c
  * @brief   LVGL demo: clock + SD capacity + board info, all in Chinese.
  *
  *  Boot order matters here:
  *
  *    1. panel        - so anything that follows has somewhere to complain to
  *    2. RTC          - LSE/LSI probing takes up to a second, get it out of
  *                      the way before the UI wants a time to show
  *    3. SD + fonts   - mounts 1: and opens GBKxx.FON; the LVGL font driver
  *                      reads glyphs straight out of those files
  *    4. LVGL         - lv_init(), display port, then the screen
  *
  *  Source files are UTF-8 and are compiled *without* -fexec-charset, so the
  *  string literals stay UTF-8 - which is what LVGL expects.  The UTF-8 code
  *  points are translated to GBK inside the font driver (see lv_gbk_map.c).
  ******************************************************************************
  */
#include "app_main.h"
#include "app_ui.h"
#include "drv_spi_oled.h"
#include "drv_oled_text.h"
#include "drv_sdio.h"
#include "drv_rtc.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_font_gbk.h"

#define LED_BLINK_MS        500U

/* LVGL asks to be serviced roughly every 5 ms; the SPI flush is blocking, so
 * there is no point spinning faster than that. */
#define LVGL_TASK_PERIOD_MS 5U

static uint32_t s_last_led_tick  = 0U;
static uint32_t s_last_lvgl_tick = 0U;

/**
  * @brief  Report the SD / font state over the debug UART.
  */
static void log_font_status(uint32_t mask)
{
    printf("[FONT] UNIGBK.BIN : %s\r\n", (mask & FONT_MASK_UNIGBK) ? "OK" : "--");
    printf("[FONT] GBK12.FON  : %s\r\n", (mask & FONT_MASK_GBK12)  ? "OK" : "--");
    printf("[FONT] GBK16.FON  : %s\r\n", (mask & FONT_MASK_GBK16)  ? "OK" : "--");
    printf("[FONT] GBK24.FON  : %s\r\n", (mask & FONT_MASK_GBK24)  ? "OK" : "--");
    printf("[FONT] GBK32.FON  : %s\r\n", (mask & FONT_MASK_GBK32)  ? "OK" : "--");
}

/**
  * @brief  Bring up the RTC and log what happened.
  */
static void init_rtc(void)
{
    rtc_datetime_t dt;

    if (drv_rtc_init() != RT_OK)
    {
        printf("[RTC ] no low-speed clock - calendar unavailable\r\n");
        return;
    }

    printf("[RTC ] clocked by %s%s\r\n",
           (drv_rtc_clock_source() == RTC_CLK_LSE) ? "LSE 32.768 kHz crystal"
                                                   : "LSI internal RC",
           (drv_rtc_clock_source() == RTC_CLK_LSI) ? " (+/-5%, will drift)" : "");

    if (drv_rtc_was_reset())
    {
        printf("[RTC ] backup domain was empty, seeded from build time\r\n");
    }

    if (drv_rtc_get(&dt) == RT_OK)
    {
        printf("[RTC ] %04u-%02u-%02u %s %02u:%02u:%02u\r\n",
               (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.day,
               drv_rtc_weekday_en(dt.weekday),
               (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
    }
}

/**
  * @brief  Log the card geometry once at boot.
  */
static void log_sd_info(void)
{
    sd_info_t info;
    char      total[16];
    char      freesz[16];
    char      card[16];
    uint32_t  t0 = HAL_GetTick();

    if (drv_sd_query_info(&info) != RT_OK)
    {
        printf("[SD  ] capacity query failed\r\n");
        return;
    }

    drv_sd_format_size(info.card_bytes,     card,   sizeof(card));
    drv_sd_format_size(info.fs_total_bytes, total,  sizeof(total));
    drv_sd_format_size(info.fs_free_bytes,  freesz, sizeof(freesz));

    printf("[SD  ] %s %s, card %s, fs %s, free %s (%lu ms)\r\n",
           drv_sd_card_name(info.card_type),
           drv_sd_fs_name(info.fs_type),
           card, total, freesz,
           (unsigned long)(HAL_GetTick() - t0));
}

void application_init(void)
{
    uint32_t mask;
    uint8_t  font_ready;

    printf("\r\n");
    printf("========================================\r\n");
    printf(" STM32H743ZIT6 - LVGL Chinese UI\r\n");
    printf(" SYSCLK : %lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    printf(" HCLK   : %lu Hz\r\n", (unsigned long)HAL_RCC_GetHCLKFreq());

    if (g_clock_source == CLOCK_SRC_HSE_XTAL)
    {
        printf(" HSE    : 25 MHz crystal OK (CSS armed)\r\n");
    }
    else
    {
        printf(" HSE    : *** CRYSTAL FAILED -> running on HSI ***\r\n");
        printf("          check X1 / load caps on PH0-PH1\r\n");
    }
    printf("========================================\r\n");

    /* 1. Panel ------------------------------------------------------------*/
    driver_spi_oled_init();
    LCD_SetBackColor(LCD_BLACK);
    LCD_Clear();
    printf("[LCD ] ST7789 240x240 on SPI6 ready\r\n");

    /* 2. RTC --------------------------------------------------------------*/
    init_rtc();

    /* 3. SD card + GBK font files ----------------------------------------*/
    if (lcd_driver_font_init() == RT_OK)
    {
        font_ready = 1U;
        mask = lcd_driver_font_status();
        printf("[SD  ] mounted, fonts loaded\r\n");
        log_font_status(mask);
        log_sd_info();
    }
    else
    {
        font_ready = 0U;
        mask = 0U;
        printf("[SD  ] mount or font open FAILED\r\n");
        printf("[SD  ] expected 1:/SYSTEM/FONT/GBKxx.FON\r\n");
    }

    /* 4. LVGL -------------------------------------------------------------*/
    lv_init();
    lv_port_disp_init();
    printf("[LVGL] v%d.%d.%d, %u KB heap\r\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
           (unsigned)(LV_MEM_SIZE / 1024U));

    if (font_ready != 0U)
    {
        app_ui_create();
    }
    else
    {
        /* ASCII-only page: the CJK glyphs are on the card that just failed. */
        app_ui_show_fault("SD card not mounted",
                          "or GBK font files",
                          "are missing.");
    }

    /* Paint the first frame before returning so the screen is never left
     * showing the power-on garbage while the main loop spins up. */
    (void)lv_timer_handler();

    s_last_lvgl_tick = HAL_GetTick();
    s_last_led_tick  = s_last_lvgl_tick;
}

void application_run(void)
{
    uint32_t now = HAL_GetTick();

    /* Crystal died while running: CSS already moved SYSCLK to HSI. */
    if (g_hse_css_fault)
    {
        g_hse_css_fault = 0U;
        printf("\r\n[CLK ] *** HSE CSS FAULT: 25 MHz crystal stopped ***\r\n");
        printf("[CLK ] hardware fell back to HSI, SYSCLK now %lu Hz\r\n",
               (unsigned long)HAL_RCC_GetSysClockFreq());
    }

    /* Heartbeat */
    if ((now - s_last_led_tick) >= LED_BLINK_MS)
    {
        s_last_led_tick = now;
        LED_TOGGLE();
    }

    /* LVGL housekeeping: timers, redraw, flush */
    if ((now - s_last_lvgl_tick) >= LVGL_TASK_PERIOD_MS)
    {
        s_last_lvgl_tick = now;
        (void)lv_timer_handler();
    }
}
