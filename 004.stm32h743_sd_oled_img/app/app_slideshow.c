/**
  ******************************************************************************
  * @file    app_slideshow.c
  * @brief   Cycles through the JPEGs found in 0:/image, one every 5 seconds.
  ******************************************************************************
  */

#include "app_slideshow.h"
#include "app_image.h"

#include "bsp_log.h"
#include "bsp_oled.h"
#include "drv_sdio.h"

#include "ff.h"

#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------- statics */

static char     s_files[SLIDESHOW_MAX_FILES][SLIDESHOW_MAX_NAME];
static uint32_t s_file_count;
static uint32_t s_index;

static uint32_t s_last_tick;        /* when the current frame went on screen */
static uint32_t s_retry_tick;       /* back-off timer while the card is down */
static int      s_ready;            /* a valid file list is loaded           */

/* Kept static: FILINFO is ~300 bytes with LFN enabled. */
static DIR      s_dir;
static FILINFO  s_fno;

/* --------------------------------------------------------------- helpers  */

/** Case-insensitive check for a .jpg / .jpeg suffix. */
static int is_jpeg_name(const char *name)
{
    size_t len = strlen(name);
    const char *ext;

    if (len < 5U)               /* shortest possible is "a.jpg" */
    {
        return 0;
    }

    ext = name + (len - 4U);
    if (((ext[0] == '.')) &&
        ((ext[1] == 'j') || (ext[1] == 'J')) &&
        ((ext[2] == 'p') || (ext[2] == 'P')) &&
        ((ext[3] == 'g') || (ext[3] == 'G')))
    {
        return 1;
    }

    if (len >= 6U)
    {
        ext = name + (len - 5U);
        if (((ext[0] == '.')) &&
            ((ext[1] == 'j') || (ext[1] == 'J')) &&
            ((ext[2] == 'p') || (ext[2] == 'P')) &&
            ((ext[3] == 'e') || (ext[3] == 'E')) &&
            ((ext[4] == 'g') || (ext[4] == 'G')))
        {
            return 1;
        }
    }

    return 0;
}

/** Build the picture list from SLIDESHOW_DIR. */
static GlobalType_t slideshow_scan(void)
{
    FRESULT fr;

    s_file_count = 0U;
    s_index      = 0U;

    fr = f_opendir(&s_dir, SLIDESHOW_DIR);
    if (fr != FR_OK)
    {
        PRINT_LOG("[E] opendir %s failed (fr=%d)\r\n", SLIDESHOW_DIR, (int)fr);
        return RT_FAIL;
    }

    for (;;)
    {
        fr = f_readdir(&s_dir, &s_fno);
        if ((fr != FR_OK) || (s_fno.fname[0] == '\0'))
        {
            break;                              /* error or end of directory */
        }
        if ((s_fno.fattrib & AM_DIR) != 0U)
        {
            continue;                           /* sub-directories ignored   */
        }
        if (!is_jpeg_name(s_fno.fname))
        {
            continue;
        }
        if (strlen(s_fno.fname) >= SLIDESHOW_MAX_NAME)
        {
            PRINT_LOG("[W] name too long, skipped: %s\r\n", s_fno.fname);
            continue;
        }

        strcpy(s_files[s_file_count], s_fno.fname);
        PRINT_LOG("[I]   [%2lu] %-32s %lu bytes\r\n",
              (unsigned long)s_file_count,
              s_files[s_file_count],
              (unsigned long)s_fno.fsize);

        s_file_count++;
        if (s_file_count >= SLIDESHOW_MAX_FILES)
        {
            PRINT_LOG("[W] file list full (%d), remaining files ignored\r\n",
                  SLIDESHOW_MAX_FILES);
            break;
        }
    }

    f_closedir(&s_dir);

    PRINT_LOG("[I] %lu jpeg file(s) in %s\r\n", (unsigned long)s_file_count, SLIDESHOW_DIR);
    return (s_file_count > 0U) ? RT_OK : RT_FAIL;
}

/** Decode + display the picture at @p idx. */
static GlobalType_t slideshow_show(uint32_t idx)
{
    char             path[sizeof(SLIDESHOW_DIR) + 1 + SLIDESHOW_MAX_NAME];
    app_image_info_t info;

    if (idx >= s_file_count)
    {
        return RT_FAIL;
    }

    (void)snprintf(path, sizeof(path), "%s/%s", SLIDESHOW_DIR, s_files[idx]);

    if (app_image_decode_file(path, &info) != RT_OK)
    {
        bsp_oled_show_banner("DECODE FAILED", s_files[idx]);
        return RT_FAIL;
    }

    bsp_oled_blit_frame(app_image_framebuffer());

    PRINT_LOG("[I] [%lu/%lu] %s  %ux%u -> 1/%u -> crop %u -> 240x240  %lums\r\n",
          (unsigned long)(idx + 1U), (unsigned long)s_file_count,
          s_files[idx],
          info.src_width, info.src_height,
          (unsigned)(1U << info.scale),
          info.crop_side,
          (unsigned long)info.elapsed_ms);

    return RT_OK;
}

/* ---------------------------------------------------------------- public  */

uint32_t app_slideshow_count(void)
{
    return s_file_count;
}

void app_slideshow_init(void)
{
    s_ready      = 0;
    s_file_count = 0U;
    s_index      = 0U;
    s_last_tick  = HAL_GetTick();
    s_retry_tick = HAL_GetTick();

    if (!bsp_sdcard_is_mounted())
    {
        bsp_oled_show_banner("NO SD CARD", "waiting for card...");
        return;
    }

    PRINT_LOG("[I] scanning %s ...\r\n", SLIDESHOW_DIR);

    if (slideshow_scan() != RT_OK)
    {
        bsp_oled_show_banner("NO PICTURES", SLIDESHOW_DIR " is empty");
        return;
    }

    s_ready = 1;

    /* First frame immediately, then one every SLIDESHOW_PERIOD_MS. */
    (void)slideshow_show(s_index);
    s_last_tick = HAL_GetTick();
}

void app_slideshow_poll(void)
{
    uint32_t now = HAL_GetTick();

    /* --- card missing or directory empty: retry once per period --------- */
    if (!s_ready)
    {
        if ((now - s_retry_tick) < SLIDESHOW_PERIOD_MS)
        {
            return;
        }
        s_retry_tick = now;

        if (!bsp_sdcard_is_mounted())
        {
            if (bsp_sdcard_mount() != RT_OK)
            {
                return;                         /* still nothing, try later */
            }
        }
        if (slideshow_scan() == RT_OK)
        {
            PRINT_LOG("[I] card back online, slideshow resumed\r\n");
            s_ready = 1;
            (void)slideshow_show(s_index);
            s_last_tick = HAL_GetTick();
        }
        return;
    }

    /* --- normal operation ----------------------------------------------- */
    if ((now - s_last_tick) < SLIDESHOW_PERIOD_MS)
    {
        return;
    }

    LED_TOGGLE();

    s_index = (s_index + 1U) % s_file_count;

    if (slideshow_show(s_index) != RT_OK)
    {
        /*
         * A single bad file must not stall the show. If the card itself
         * disappeared, drop back to the retry path.
         */
        if (!bsp_sdcard_is_mounted())
        {
            PRINT_LOG("[E] card lost, going back to retry mode\r\n");
            bsp_oled_show_banner("SD CARD LOST", "reinsert the card");
            s_ready      = 0;
            s_retry_tick = HAL_GetTick();
        }
    }

    s_last_tick = HAL_GetTick();
}
