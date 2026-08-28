/**
  ******************************************************************************
  * @file    ui_task.c
  * @brief   LVGL rendering thread + system start-up loader for the F429 panel.
  *
  *  Everything here runs after the FreeRTOS scheduler is up.
  *
  *  Flow
  *  ----
  *    1. lcd_driver_init()            FMC Bank1 NE1 + panel init (backlight, black)
  *    2. lv_init() + lv_port_disp_init()   draw buffer in SDRAM
  *    3. app_ui_show_centered("wait for system start...")
  *       -> pure ASCII, rendered from the compiled-in ASCII tables, so it is
  *          correct with no font file and no storage at all
  *    4. loader loop:
  *         a. probe the microSD card (SDIO) every SD_RETRY_MS until it answers
  *         b. as soon as the card is up, open <SD>:/SYSTEM/FONT/GBKxx.FON
  *         c. if the card never comes up, wait for the USB mass-storage volume
  *            and open <USB>:/SYSTEM/FONT/GBKxx.FON
  *         d. fonts available -> app_ui_create() (the Chinese status panel)
  *         e. LOADER_TIMEOUT_MS with no fonts -> app_ui_show_centered(
  *              "sdcard and usb loader failed!")
  *    5. recovery: after the failure screen the probe keeps running, so
  *       plugging a card / U-disk in later still reaches the main screen
  *    6. forever: lv_timer_handler() every LVGL_TASK_PERIOD_MS
  *
  *  Timing
  *  ------
  *  The 10 s deadline is measured from the first frame of the boot screen, not
  *  from reset, so slow panel bring-up does not eat into the budget.
  ******************************************************************************
  */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"
#include "lv_font_gbk.h"

#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "lv_port_disp.h"
#include "usb_host_app.h"
#include "fs_diskio.h"
#include "sd_card.h"
#include "app_ui.h"

#include "ui_task.h"

/* How long the boot screen may stay up before the loader gives up. */
#define LOADER_TIMEOUT_MS    10000U

/* How often the SD socket is re-probed while the boot screen is up. */
#define SD_RETRY_MS            500U

/* After the deadline the probe slows down: still alive (so a card plugged in
 * later is picked up) but no longer competing for console bandwidth. */
#define SD_RETRY_SLOW_MS      3000U

/* Set to 0 to disable the USB fallback.  Only used by the acceptance test:
 * with the U-disk physically plugged in that is the only way to reproduce the
 * "no storage at all" case and prove the 10 s timeout branch. */
#ifndef LOADER_ENABLE_USB_FALLBACK
#define LOADER_ENABLE_USB_FALLBACK   1
#endif

/* -------------------------------------------------------------------------- */
/* Loader state                                                               */
/* -------------------------------------------------------------------------- */
typedef enum
{
    LOAD_BOOT = 0,      /* boot screen, still probing for fonts            */
    LOAD_OK,            /* fonts loaded, status panel on screen            */
    LOAD_FAILED         /* deadline hit, failure screen (recovery still on) */
} load_state_t;

static load_state_t s_state        = LOAD_BOOT;
static uint8_t      s_sd_up        = 0U;   /* SD card mounted                */
static uint8_t      s_sd_tried     = 0U;   /* SD font open already attempted */
static uint8_t      s_usb_tried    = 0U;   /* USB font open already attempted */
static TickType_t   s_sd_last_try  = 0U;
static TickType_t   s_deadline_at  = 0U;

/* One-shot acceptance probe: how long after the status panel went up the
 * glyph cache is sampled (see GLYPH_PROOF_DELAY_MS). */
#define GLYPH_PROOF_DELAY_MS  1500U
static TickType_t   s_main_at      = 0U;
static uint8_t      s_glyph_proof  = 0U;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

/**
  * @brief  Open the GBK font files on a volume that is already mounted.
  * @retval 1 on success (status panel may be built), 0 on failure.
  */
static int try_fonts(const char *vol)
{
    if (lcd_driver_font_init(vol) != RT_OK)
    {
        printf("[FONT] no usable GBK fonts on %s\r\n", vol);
        return 0;
    }

    printf("[FONT] source=%s mask=0x%02X\r\n", vol,
           (unsigned int)lcd_driver_font_status());

    /* The glyph cache is per-size and content-addressed by GBK code; drop it so
     * nothing cached from a previous (possibly different) volume survives. */
    lv_font_gbk_reset_cache();

    return 1;
}

/* -------------------------------------------------------------------------- */
/* Task body                                                                  */
/* -------------------------------------------------------------------------- */
void ui_task(void *arg)
{
    (void)arg;
    usb_state_t last_usb = USB_DISCONNECTED;

    printf("[UI  ] starting LCD + LVGL bring-up\r\n");

    /* 1. Panel hardware.  Safe to do immediately; needs no storage. */
    if (lcd_driver_init() != RT_OK)
    {
        printf("[UI  ] LCD driver init FAILED - parking task\r\n");
        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* 2. LVGL core + display port.  The draw buffer is pvPortMalloc'd in SDRAM
     *    by lv_port_disp_init(); the 5 ms render pump is at the bottom. */
    lv_init();
    lv_port_disp_init();

    /* 3. Boot screen: ASCII only, no font file required. */
    app_ui_show_centered("wait for system start...");
    lv_timer_handler();                 /* push it out before any slow probe */
    printf("[UI  ] boot screen: wait for system start...\r\n");

    s_deadline_at = xTaskGetTickCount();

    /* Backdate the SD probe timer by one retry interval so the very first loop
     * pass probes the card immediately (unsigned wrap-around makes this correct
     * even when the tick counter is still small).  The requirement is
     * "SD card first, USB only as a fallback", so the card must be tried before
     * the U-disk enumeration can win the race. */
    s_sd_last_try = s_deadline_at - pdMS_TO_TICKS(SD_RETRY_MS);

    for (;;)
    {
        TickType_t now = xTaskGetTickCount();

        /* ---- Keep the USB status line in sync (main screen only) -------- */
        if (g_usb_state != last_usb)
        {
            last_usb = g_usb_state;
            if (s_state == LOAD_OK)
            {
                app_ui_request_usb_refresh();
            }
        }

        if (s_state != LOAD_OK)
        {
            /* ---- a. probe the microSD card ------------------------------
             * Retry until the card answers once; after that s_sd_tried is set
             * and the socket is left alone (a card that mounts but carries no
             * font files is not re-probed every 500 ms). */
            if ((s_sd_up == 0U) && (s_sd_tried == 0U) &&
                ((now - s_sd_last_try) >= pdMS_TO_TICKS(
                    (s_state == LOAD_FAILED) ? SD_RETRY_SLOW_MS : SD_RETRY_MS)))
            {
                s_sd_last_try = now;
                if (sd_card_init() == RT_OK)
                {
                    s_sd_up = 1U;
                }
                else
                {
                    /* Report an empty socket once, then stay quiet: the probe
                     * runs every few hundred ms and would otherwise flood the
                     * console.  Recovery still works, it just stops talking. */
                    sd_card_set_quiet(1);
                }
            }

            /* ---- b. fonts from the card (first choice) ------------------ */
            if ((s_sd_up != 0U) && (s_sd_tried == 0U))
            {
                s_sd_tried = 1U;
                printf("[FONT] trying microSD " FS_VOL_SD "/SYSTEM/FONT/\r\n");
                if (try_fonts(FS_VOL_SD) != 0)
                {
                    app_ui_create();
                    s_state   = LOAD_OK;
                    s_main_at = now;
                    printf("[UI  ] main screen (fonts from " FS_VOL_SD ")\r\n");
                }
                else
                {
                    /* Card is mounted but the font files are not there: give
                     * the USB volume its chance as soon as it appears. */
                    sd_card_invalidate();
                    s_sd_up = 0U;
                }
            }

            /* ---- c. fonts from the U-disk (fallback) -------------------- */
            if (LOADER_ENABLE_USB_FALLBACK &&
                (s_state != LOAD_OK) && (s_usb_tried == 0U) &&
                (g_usb_state == USB_MOUNTED))
            {
                s_usb_tried = 1U;
                printf("[FONT] trying USB " FS_VOL_USB "/SYSTEM/FONT/\r\n");
                if (try_fonts(FS_VOL_USB) != 0)
                {
                    app_ui_create();
                    s_state   = LOAD_OK;
                    s_main_at = now;
                    printf("[UI  ] main screen (fonts from " FS_VOL_USB ")\r\n");
                }
            }

            /* ---- d. deadline -> failure screen --------------------------
             * Guarded on LOAD_BOOT, not "!= LOAD_OK": LOAD_FAILED also fails
             * that test, which would re-arm the screen (and re-print) on
             * every single iteration. */
            if ((s_state == LOAD_BOOT) &&
                ((now - s_deadline_at) >= pdMS_TO_TICKS(LOADER_TIMEOUT_MS)))
            {
                app_ui_show_centered("sdcard and usb loader failed!");
                s_state = LOAD_FAILED;
                /* Slow the probe down now that the boot phase is over. */
                s_sd_last_try = now;
                printf("[UI  ] timeout: sdcard and usb loader failed!\r\n");
            }
        }

        /* 6. Render pump.  LV_TICK_CUSTOM rides on HAL_GetTick() (TIM7), so
         *    no lv_tick_inc() is required here. */
        /* Acceptance probe: 1.5 s after the status panel went up, report the
         * glyph cache counters.  A non-zero miss count proves Chinese glyphs
         * were really fetched from the font files on the volume -- i.e. the
         * panel is rendering Chinese and not just blank boxes. */
        if ((s_state == LOAD_OK) && (s_glyph_proof == 0U) && (s_main_at != 0U) &&
            ((now - s_main_at) >= pdMS_TO_TICKS(GLYPH_PROOF_DELAY_MS)))
        {
            uint32_t hits = 0U, miss = 0U;

            s_glyph_proof = 1U;
            lv_font_gbk_cache_stats(&hits, &miss);
            printf("[UI  ] glyph cache: hits=%lu misses=%lu (non-zero misses => CJK rendered)\r\n",
                   (unsigned long)hits, (unsigned long)miss);
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_PERIOD_MS));
    }
}
