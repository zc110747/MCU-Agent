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
 *    3. SD + fonts   - mounts 1: and opens GBKxx.FON
 *    4. LVGL         - lv_init(), display port, cached FS port, font engine,
 *                      then the screen
 *
 *  Source files are UTF-8 and are compiled with -fexec-charset=UTF-8, so the
 *  string literals stay UTF-8 - which is what LVGL expects.  The GBK engine
 *  translates each code point to the GBK index GBKxx.FON uses (lv_gbk_map.c);
 *  the HarmonyOS engine feeds the code point straight to the .ttf rasteriser.
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
#include "lv_port_fs.h"
#include "lv_font_provider.h"
#include "lv_font_cfg.h"
#include "lvgl_font.h"

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

/**
  * @brief  Dump the CTF/TTF block-cache counters.
  */
static void log_ctf_stats(const char *tag)
{
    lvgl_font_stats_t st;

    lvgl_font_get_stats(&st);

    printf("[CTF ] %s: lookup %lu, not-found %lu, io-err %lu\r\n",
           tag, (unsigned long)st.ctf_lookups,
           (unsigned long)st.ctf_not_found,
           (unsigned long)st.ctf_io_errors);
    printf("[CTF ] %s: ttf hit %lu, miss %lu, f_read %lu, %lu B\r\n",
           tag, (unsigned long)st.ttf_hits, (unsigned long)st.ttf_misses,
           (unsigned long)st.ttf_fills, (unsigned long)st.ttf_bytes);
    printf("[CTF ] %s: sd wait %lu us (lseek %lu + read %lu)\r\n",
           tag, (unsigned long)(st.ttf_seek_us + st.ttf_read_us),
           (unsigned long)st.ttf_seek_us, (unsigned long)st.ttf_read_us);
    printf("[CTF ] %s: bitmap hit %lu, miss %lu, flush %lu, %lu B held\r\n",
           tag, (unsigned long)st.bmp_hits, (unsigned long)st.bmp_misses,
           (unsigned long)st.bmp_flushes, (unsigned long)st.bmp_bytes);
    printf("[CTF ] %s: stb arena peak %lu B, overflow %lu\r\n",
           tag, (unsigned long)st.arena_peak, (unsigned long)st.arena_fails);
}

/**
  * @brief  Rasterise a fixed vector on the real board and print what it cost.
  *
  *  The acceptance invariant is the last line: a code point the index does not
  *  carry must not cost a single SD read, because it never reaches the TTF.
  */
static void log_ctf_probe(void)
{
    lvgl_font_probe_t p[16];
    uint32_t          n;
    uint32_t          i;
    uint32_t          nf        = 0u;
    uint32_t          nf_reads  = 0u;
    uint32_t          nf_latin  = 0u;
    uint32_t          cold_max  = 0u;
    uint32_t          cold_tot  = 0u;
    uint32_t          cold_n    = 0u;
    uint32_t          sd_tot    = 0u;

    n = lvgl_font_selftest(p, (uint32_t)(sizeof(p) / sizeof(p[0])));

    printf("\r\n[CTF ] on-target probe, %u code points\r\n", (unsigned)n);
    printf("[CTF ]  U+xxxxx  px  src adv  box     ofs      cold us  sd us raster   ink   SD\r\n");

    for (i = 0u; i < n; i++)
    {
        const lvgl_font_probe_t *e = &p[i];
        uint32_t                 raster = (e->cold_us > e->sd_us)
                                        ? (e->cold_us - e->sd_us) : 0u;

        printf("[CTF ]  U+%05lX %4lu  %s %4lu %3lux%-3lu %+4d,%-4d %7lu %6lu %6lu %6lu %4lu\r\n",
               (unsigned long)e->cp, (unsigned long)e->px,
               (e->found != 0u) ? ((e->empty != 0u) ? "EMP" : "CTF")
                                : ((e->fb_adv_w != 0u) ? "FAL" : " -- "),
               (unsigned long)((e->found != 0u) ? e->adv_w : e->fb_adv_w),
               (unsigned long)e->box_w, (unsigned long)e->box_h,
               (int)e->ofs_x, (int)e->ofs_y,
               (unsigned long)e->cold_us, (unsigned long)e->sd_us,
               (unsigned long)raster,
               (unsigned long)e->ink, (unsigned long)e->sd_reads);

        if (e->found == 0u)
        {
            nf++;
            nf_reads += e->sd_reads;
            if (e->fb_adv_w != 0u)
            {
                nf_latin++;
            }
        }
        else
        {
            if (e->cold_us > cold_max)
            {
                cold_max = e->cold_us;
            }
            cold_tot += e->cold_us;
            sd_tot   += e->sd_us;
            cold_n++;
        }
    }

    printf("[CTF ] found %lu, NOT_FOUND %lu (of which Latin fallback %lu)\r\n",
           (unsigned long)(n - nf), (unsigned long)nf, (unsigned long)nf_latin);
    printf("[CTF ] cold raster: avg %lu us, worst %lu us (sd %lu us, cpu %lu us)\r\n",
           (unsigned long)((cold_n != 0u) ? (cold_tot / cold_n) : 0u),
           (unsigned long)cold_max, (unsigned long)sd_tot,
           (unsigned long)((cold_tot > sd_tot) ? (cold_tot - sd_tot) : 0u));
    printf("[CTF ] NOT_FOUND cost %lu SD reads (must be 0): %s\r\n",
           (unsigned long)nf_reads, (nf_reads == 0u) ? "PASS" : "FAIL");
}

/**
  * @brief  Log the LVGL heap: how much is left and how fragmented it got.
  */
static void log_lvgl_heap(void)
{
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);
    printf("[LVGL] heap: %u B free of %u B, max block %u B, used %u%%, frag %u%%\r\n",
           (unsigned)mon.free_size,
           (unsigned)mon.total_size,
           (unsigned)mon.free_biggest_size,
           (unsigned)mon.used_pct,
           (unsigned)mon.frag_pct);
}

void application_init(void)
{
    uint32_t mask;
    uint8_t  font_ready;
    uint32_t t0;

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

    /* The font engines read through LVGL's file system, so the cached FatFs
     * driver has to be up before they are initialised. */
    lv_port_fs_init();

    if (font_ready != 0U)
    {
        /* Picks LV_FONT_ENGINE (default: HarmonyOS Sans TC) and degrades to the
         * GBK bitmaps on its own if the .ttf is not there. */
        (void)lv_font_provider_init();

        /* lv_disp_drv_register() already installed a theme with LV_FONT_DEFAULT
         * (the GBK 16).  Re-running the init now swaps the theme's font for the
         * engine we actually ended up with, so widgets that carry no text style
         * of their own also get HarmonyOS. */
        lv_theme_default_init(lv_disp_get_default(),
                              lv_palette_main(LV_PALETTE_BLUE),
                              lv_palette_main(LV_PALETTE_RED),
                              LV_THEME_DEFAULT_DARK,
                              lv_font_provider_default());
    }

    if (font_ready != 0U)
    {
        t0 = HAL_GetTick();
        app_ui_create();
        printf("[UI  ] built in %lu ms\r\n",
               (unsigned long)(HAL_GetTick() - t0));
    }
    else
    {
        /* ASCII-only page: the CJK glyphs are on the card that just failed. */
        app_ui_show_fault("SD card not mounted",
                          "or GBK font files",
                          "are missing.");
    }

    /* Paint the first frame before returning so the screen is never left
     * showing the power-on garbage while the main loop spins up.  With a cold
     * glyph cache this is where every character is rasterised once. */
    t0 = HAL_GetTick();
    (void)lv_timer_handler();
    printf("[UI  ] first frame in %lu ms\r\n",
           (unsigned long)(HAL_GetTick() - t0));

    log_lvgl_heap();

#if LV_FONT_ENGINE == LV_FONT_ENGINE_CTF
    if (lv_font_provider_engine() == FONT_ENGINE_CTF)
    {
        /* The probe flushes the glyph cache, so run it after the first frame -
         * it measures cold rasterisation, not a cache hit. */
        log_ctf_probe();
        log_ctf_stats("probe");
        log_lvgl_heap();
    }
#endif

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
