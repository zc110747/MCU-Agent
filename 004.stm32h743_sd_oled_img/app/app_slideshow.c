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

/* 幻灯片播放列表：文件表、当前索引与扫描暂存（FILINFO 约 300 B，静态持有） */
typedef struct
{
    char     files[SLIDESHOW_MAX_FILES][SLIDESHOW_MAX_NAME];
    uint32_t count;
    uint32_t index;
    DIR      dir;
    FILINFO  fno;
} slideshow_list_t;

static slideshow_list_t g_list = {0};

/* 播放节拍：就绪标志、当前帧计时与卡片掉线退避计时 */
typedef struct
{
    int      ready;
    uint32_t last_tick;
    uint32_t retry_tick;
} slideshow_play_t;

static slideshow_play_t g_play = {0};

/* --------------------------------------------------------------- helpers  */

/** Case-insensitive check for a .jpg / .jpeg suffix. */
static int is_jpeg_name(const char *name)
{
    size_t      len = strlen(name);
    const char *ext;

    if (len < 5U) /* shortest possible is "a.jpg" */
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

    g_list.count = 0U;
    g_list.index      = 0U;

    fr = f_opendir(&g_list.dir, SLIDESHOW_DIR);
    if (fr != FR_OK)
    {
        PRINT_LOG("[E] opendir %s failed (fr=%d)\r\n", SLIDESHOW_DIR, (int)fr);
        return RT_FAIL;
    }

    for (;;)
    {
        fr = f_readdir(&g_list.dir, &g_list.fno);
        if ((fr != FR_OK) || (g_list.fno.fname[0] == '\0'))
        {
            break; /* error or end of directory */
        }
        if ((g_list.fno.fattrib & AM_DIR) != 0U)
        {
            continue; /* sub-directories ignored   */
        }
        if (!is_jpeg_name(g_list.fno.fname))
        {
            continue;
        }
        if (strlen(g_list.fno.fname) >= SLIDESHOW_MAX_NAME)
        {
            PRINT_LOG("[W] name too long, skipped: %s\r\n", g_list.fno.fname);
            continue;
        }

        strcpy(g_list.files[g_list.count], g_list.fno.fname);
        PRINT_LOG("[I]   [%2lu] %-32s %lu bytes\r\n",
                  (unsigned long)g_list.count,
                  g_list.files[g_list.count],
                  (unsigned long)g_list.fno.fsize);

        g_list.count++;
        if (g_list.count >= SLIDESHOW_MAX_FILES)
        {
            PRINT_LOG("[W] file list full (%d), remaining files ignored\r\n",
                      SLIDESHOW_MAX_FILES);
            break;
        }
    }

    f_closedir(&g_list.dir);

    PRINT_LOG("[I] %lu jpeg file(s) in %s\r\n", (unsigned long)g_list.count, SLIDESHOW_DIR);
    return (g_list.count > 0U) ? RT_OK : RT_FAIL;
}

/** Decode + display the picture at @p idx. */
static GlobalType_t slideshow_show(uint32_t idx)
{
    char             path[sizeof(SLIDESHOW_DIR) + 1 + SLIDESHOW_MAX_NAME];
    app_image_info_t info;

    if (idx >= g_list.count)
    {
        return RT_FAIL;
    }

    (void)snprintf(path, sizeof(path), "%s/%s", SLIDESHOW_DIR, g_list.files[idx]);

    if (app_image_decode_file(path, &info) != RT_OK)
    {
        bsp_oled_show_banner("DECODE FAILED", g_list.files[idx]);
        return RT_FAIL;
    }

    bsp_oled_blit_frame(app_image_framebuffer());

    PRINT_LOG("[I] [%lu/%lu] %s  %ux%u -> 1/%u -> crop %u -> 240x240  %lums\r\n",
              (unsigned long)(idx + 1U), (unsigned long)g_list.count,
              g_list.files[idx],
              info.src_width, info.src_height,
              (unsigned)(1U << info.scale),
              info.crop_side,
              (unsigned long)info.elapsed_ms);

    return RT_OK;
}

/* ---------------------------------------------------------------- public  */

uint32_t app_slideshow_count(void)
{
    return g_list.count;
}

void app_slideshow_init(void)
{
    g_play.ready      = 0;
    g_list.count = 0U;
    g_list.index      = 0U;
    g_play.last_tick  = HAL_GetTick();
    g_play.retry_tick = HAL_GetTick();

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

    g_play.ready = 1;

    /* First frame immediately, then one every SLIDESHOW_PERIOD_MS. */
    (void)slideshow_show(g_list.index);
    g_play.last_tick = HAL_GetTick();
}

void app_slideshow_poll(void)
{
    uint32_t now = HAL_GetTick();

    /* --- card missing or directory empty: retry once per period --------- */
    if (!g_play.ready)
    {
        if ((now - g_play.retry_tick) < SLIDESHOW_PERIOD_MS)
        {
            return;
        }
        g_play.retry_tick = now;

        if (!bsp_sdcard_is_mounted())
        {
            if (bsp_sdcard_mount() != RT_OK)
            {
                return; /* still nothing, try later */
            }
        }
        if (slideshow_scan() == RT_OK)
        {
            PRINT_LOG("[I] card back online, slideshow resumed\r\n");
            g_play.ready = 1;
            (void)slideshow_show(g_list.index);
            g_play.last_tick = HAL_GetTick();
        }
        return;
    }

    /* --- normal operation ----------------------------------------------- */
    if ((now - g_play.last_tick) < SLIDESHOW_PERIOD_MS)
    {
        return;
    }

    LED_TOGGLE();

    g_list.index = (g_list.index + 1U) % g_list.count;

    if (slideshow_show(g_list.index) != RT_OK)
    {
        /*
         * A single bad file must not stall the show. If the card itself
         * disappeared, drop back to the retry path.
         */
        if (!bsp_sdcard_is_mounted())
        {
            PRINT_LOG("[E] card lost, going back to retry mode\r\n");
            bsp_oled_show_banner("SD CARD LOST", "reinsert the card");
            g_play.ready      = 0;
            g_play.retry_tick = HAL_GetTick();
        }
    }

    g_play.last_tick = HAL_GetTick();
}
