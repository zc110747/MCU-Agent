/**
 ******************************************************************************
 * @file    image_sd.c
 * @brief   LVGL images streamed from the microSD card.
 *
 *  Two different APIs are used on purpose:
 *
 *   - LVGL's own FatFs driver (drive letter 'S') for the pixels.  It keeps a
 *     file handle open on the image and re-reads it line by line while drawing,
 *     which is exactly what a full-screen wallpaper needs on a part with 192 KB
 *     of internal RAM.
 *   - FatFs directly for existence / directory probing, because we want a
 *     clean "file missing" answer instead of LVGL's silent no-op.
 *
 *  Everything here runs in the UI task, which is also the only task that
 *  triggers glyph reads, so no extra locking is required around lv_fs calls.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "ff.h"

#include "fs_diskio.h"
#include "sd_card.h"
#include "image_sd.h"
#include "log.h"

#define IMAGE_SD_MAX_NAME  32

static uint8_t s_ready = 0U;
static uint8_t s_bg_present = 0U;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */
static int image_exists(const char *path)
{
    lv_fs_file_t f;
    lv_fs_res_t  res = lv_fs_open(&f, path, LV_FS_MODE_RD);

    if (res != LV_FS_RES_OK)
    {
        return 0;
    }
    (void)lv_fs_close(&f);
    return 1;
}

/** Log every file in 0:/IMG (names + sizes), so a missing image is obvious. */
static void log_image_dir(void)
{
    DIR     dir;
    FILINFO info;
    FRESULT rc;
    unsigned count = 0U;

    fs_lock();
    rc = f_opendir(&dir, IMAGE_SD_DIR);
    if (rc != FR_OK)
    {
        fs_unlock();
        PRINT_LOG("[IMG ] %s not found (rc=%d) - wallpaper/icons disabled\r\n",
                  IMAGE_SD_DIR, (int32_t)rc);
        return;
    }

    PRINT_LOG("[IMG ] %s:\r\n", IMAGE_SD_DIR);
    for (;;)
    {
        rc = f_readdir(&dir, &info);
        if ((rc != FR_OK) || (info.fname[0] == '\0'))
        {
            break;
        }
        if ((info.fattrib & AM_DIR) != 0U)
        {
            PRINT_LOG("[IMG ]   <DIR> %s\r\n", info.fname);
        }
        else
        {
            PRINT_LOG("[IMG ]   %-16s %lu B\r\n", info.fname,
                      (unsigned long)info.fsize);
        }
        count++;
    }
    (void)f_closedir(&dir);
    fs_unlock();

    PRINT_LOG("[IMG ] %u entr%s\r\n", count, (count == 1U) ? "y" : "ies");
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
int image_sd_init(void)
{
    if (sd_card_is_ready() == 0)
    {
        s_ready = 0U;
        return -1;
    }

    log_image_dir();

    s_bg_present = (uint8_t)image_exists(IMAGE_SD_BG_PATH);
    s_ready      = 1U;

    PRINT_LOG("[IMG ] wallpaper %s\r\n",
              (s_bg_present != 0U) ? IMAGE_SD_BG_PATH : "missing (solid colour)");
    return 0;
}

int image_sd_ready(void)
{
    return (int)s_ready;
}

lv_obj_t *image_sd_bg(lv_obj_t *parent)
{
    lv_obj_t *img;

    if ((parent == NULL) || (s_bg_present == 0U))
    {
        return NULL;
    }

    img = lv_img_create(parent);
    lv_img_set_src(img, IMAGE_SD_BG_PATH);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(img);
    return img;
}

lv_obj_t *image_sd_icon(lv_obj_t *parent, const char *name)
{
    char path[IMAGE_SD_MAX_NAME];
    lv_obj_t *img;

    if ((parent == NULL) || (name == NULL) || (s_ready == 0U))
    {
        return NULL;
    }

    (void)lv_snprintf(path, sizeof(path), "S:/IMG/%s.bin", name);
    if (image_exists(path) == 0)
    {
        return NULL;
    }

    img = lv_img_create(parent);
    lv_img_set_src(img, path);
    return img;
}
