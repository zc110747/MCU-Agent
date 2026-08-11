/**
  ******************************************************************************
  * @file    app_main.c
  * @brief   Boot sequence and cooperative main loop.
  *
  *  Boot order matters here:
  *
  *    1. console     - UART + USB CDC, so everything that follows has somewhere
  *                     to complain to (printf lands on both cables)
  *    2. panel       - the operator gets a picture as early as possible
  *    3. RTC         - LSE/LSI probing takes up to a second, get it out of the
  *                     way before the UI wants a time to show
  *    4. SD + fonts  - mounts 1: and opens GBKxx.FON; the LVGL font driver
  *                     reads glyphs straight out of those files
  *    5. LVGL + menu - lv_init(), display port, page registry, root menu
  *
  *  The loop is cooperative and single threaded.  Two modes share it:
  *
  *    - menu mode      : LVGL is serviced every LVGL_TASK_PERIOD_MS
  *    - full-screen page: the page owns the panel (the NES emulator pushes its
  *                     own frames through LCD_CopyBuffer), so lv_timer_handler
  *                     is skipped entirely - blending a 240x240 frame through
  *                     LVGL would eat the whole SPI budget for nothing
  *
  *  Source files are UTF-8 and are compiled *without* -fexec-charset, so the
  *  string literals stay UTF-8 - which is what LVGL expects.  The UTF-8 code
  *  points are translated to GBK inside the font driver (see lv_gbk_map.c).
  ******************************************************************************
  */
#include "app_main.h"
#include "app_page.h"
#include "app_cmd.h"
#include "bsp_console.h"
#include "bsp_key.h"
#include "drv_usb_cdc.h"
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

/* Events consumed per loop pass.  A host holding the auto-repeat key down can
 * queue faster than the UI redraws; capping keeps the frame pacing honest. */
#define KEY_EVENTS_PER_PASS 4U

static uint32_t s_last_led_tick  = 0U;
static uint32_t s_last_lvgl_tick = 0U;
static uint8_t  s_font_ready     = 0U;

/*----------------------------------------------------------------------------
 *  Boot logging
 *--------------------------------------------------------------------------*/

static void log_font_status(uint32_t mask)
{
    printf("[FONT] UNIGBK.BIN : %s\r\n", (mask & FONT_MASK_UNIGBK) ? "OK" : "--");
    printf("[FONT] GBK12.FON  : %s\r\n", (mask & FONT_MASK_GBK12)  ? "OK" : "--");
    printf("[FONT] GBK16.FON  : %s\r\n", (mask & FONT_MASK_GBK16)  ? "OK" : "--");
    printf("[FONT] GBK24.FON  : %s\r\n", (mask & FONT_MASK_GBK24)  ? "OK" : "--");
    printf("[FONT] GBK32.FON  : %s\r\n", (mask & FONT_MASK_GBK32)  ? "OK" : "--");
}

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

/*----------------------------------------------------------------------------
 *  Fault screen
 *--------------------------------------------------------------------------*/

/**
  * @brief  ASCII-only page shown when the card (and therefore the CJK glyphs)
  *         is missing.
  * @note   lv_font_gbk_16 is still usable here: its ASCII range 0x20..0x7E is
  *         served from the compiled-in tables in drv_oled_fonts.c, and only
  *         the CJK range needs the GBKxx.FON files that just failed to open.
  */
static void show_fault(const char *l1, const char *l2, const char *l3)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    ui_label_center(scr,  60, &lv_font_gbk_16, COL_ERR,  "STORAGE FAULT");
    ui_label_center(scr, 100, &lv_font_gbk_16, COL_TEXT, l1);
    ui_label_center(scr, 122, &lv_font_gbk_16, COL_TEXT, l2);
    ui_label_center(scr, 144, &lv_font_gbk_16, COL_TEXT, l3);
    ui_label_center(scr, 190, &lv_font_gbk_16, COL_DIM,  "console still works");
}

/*----------------------------------------------------------------------------
 *  Page registry
 *--------------------------------------------------------------------------*/

/**
  * @brief  Registration order is menu order.  Everything the operator is most
  *         likely to reach for goes first, "about" last.
  */
static void register_pages(void)
{
    app_menu_register(page_nes_get());
    app_menu_register(page_image_get());
    app_menu_register(page_clock_get());
    app_menu_register(page_sysinfo_get());
    app_menu_register(page_keytest_get());
    app_menu_register(page_about_get());
}

/*----------------------------------------------------------------------------
 *  Entry points
 *--------------------------------------------------------------------------*/

void application_init(void)
{
    uint32_t mask;

    /* 1. Console: UART first (always present), then USB (may fail on a board
     *    with no cable plugged in - not fatal, the UART carries on). ---------*/
    bsp_console_init();
    bsp_key_init();

    printf("\r\n");
    printf("========================================\r\n");
    printf(" %s %s - STM32H743ZIT6\r\n", APP_FW_NAME, APP_FW_VERSION);
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

    if (drv_usb_cdc_init() == 0)
    {
        printf("[USB ] CDC device on PA11/PA12, waiting for host\r\n");
    }
    else
    {
        printf("[USB ] init FAILED - console is UART only\r\n");
    }

    /* 2. Panel ------------------------------------------------------------*/
    driver_spi_oled_init();
    LCD_SetBackColor(LCD_BLACK);
    LCD_Clear();
    printf("[LCD ] ST7789 240x240 on SPI6 ready\r\n");

    /* 3. RTC --------------------------------------------------------------*/
    init_rtc();

    /* 4. SD card + GBK font files ----------------------------------------*/
    if (lcd_driver_font_init() == RT_OK)
    {
        s_font_ready = 1U;
        mask = lcd_driver_font_status();
        printf("[SD  ] mounted, fonts loaded\r\n");
        log_font_status(mask);
        log_sd_info();
    }
    else
    {
        s_font_ready = 0U;
        printf("[SD  ] mount or font open FAILED\r\n");
        printf("[SD  ] expected 1:/SYSTEM/FONT/GBKxx.FON\r\n");
    }

    /* 5. LVGL + menu ------------------------------------------------------*/
    lv_init();
    lv_port_disp_init();
    printf("[LVGL] v%d.%d.%d, %u KB heap\r\n",
           LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
           (unsigned)(LV_MEM_SIZE / 1024U));

    if (s_font_ready != 0U)
    {
        register_pages();
        app_menu_init();
        printf("[MENU] %d pages registered\r\n", app_menu_count());

        /* Boot straight into the clock "watch" face; BACK returns to menu. */
        (void)app_menu_open_cmd("clock");
    }
    else
    {
        /* No pages: the menu labels are Chinese and would come out blank. */
        show_fault("SD card not mounted", "or GBK font files", "are missing.");
    }

    /* Paint the first frame before returning so the screen is never left
     * showing the power-on garbage while the main loop spins up. */
    (void)lv_timer_handler();

    app_cmd_init();

    s_last_lvgl_tick = HAL_GetTick();
    s_last_led_tick  = s_last_lvgl_tick;
}

void application_run(void)
{
    uint32_t    now;
    key_event_t ev;
    uint32_t    handled = 0U;

    /* 1. Input --------------------------------------------------------------
     * The USB stack must be pumped every pass or control transfers time out
     * and the host drops the port; the emulator's frame loop below is fast
     * enough (16 ms) that this stays comfortable. */
    bsp_console_task();
    app_cmd_task();
    bsp_key_poll();

    while ((handled++ < KEY_EVENTS_PER_PASS) && (bsp_key_pop(&ev) != 0))
    {
        app_menu_handle_key((key_id_t)ev.id, (key_edge_t)ev.edge);
    }

    /* 2. Active page --------------------------------------------------------
     * Runs the emulator frame when the NES page is up, refreshes the clock
     * when the clock page is up, nothing at all in the root menu. */
    app_menu_tick();

    now = HAL_GetTick();

    /* Crystal died while running: CSS already moved SYSCLK to HSI. */
    if (g_hse_css_fault)
    {
        g_hse_css_fault = 0U;
        printf("\r\n[CLK ] *** HSE CSS FAULT: 25 MHz crystal stopped ***\r\n");
        printf("[CLK ] hardware fell back to HSI, SYSCLK now %lu Hz\r\n",
               (unsigned long)HAL_RCC_GetSysClockFreq());
    }

    /* 3. Heartbeat ----------------------------------------------------------*/
    if ((now - s_last_led_tick) >= LED_BLINK_MS)
    {
        s_last_led_tick = now;
        LED_TOGGLE();
    }

    /* 4. LVGL ---------------------------------------------------------------
     * Skipped while a page owns the panel: lv_timer_handler() would flush a
     * stale widget tree straight over the video frame the page just pushed. */
    if (app_menu_is_full_screen() == 0)
    {
        if ((now - s_last_lvgl_tick) >= LVGL_TASK_PERIOD_MS)
        {
            s_last_lvgl_tick = now;
            (void)lv_timer_handler();
        }
    }
    else
    {
        s_last_lvgl_tick = now;
    }
}
