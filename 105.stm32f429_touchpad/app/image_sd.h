/**
 ******************************************************************************
 * @file    image_sd.h
 * @brief   Images stored on the microSD card, drawn with LVGL.
 *
 *  Images are LVGL binary files ("image converter" output, .bin) copied into
 *  0:/IMG on the card.  They are NOT compiled into flash, so the wallpaper and
 *  the icon set can be replaced by pulling the card:
 *
 *      0:/IMG/bg.bin        800x480 wallpaper (optional)
 *      0:/IMG/<name>.bin    any icon (size comes from the file header)
 *
 *  The extension must be lower case: LVGL's built-in decoder only accepts
 *  "*.bin" and compares the extension verbatim.  FAT itself is case
 *  insensitive, so the file may be stored as BG.BIN.
 *
 *  LVGL reads them through its FatFs driver (lv_conf.h: LV_USE_FS_FATFS = 1,
 *  drive letter 'S'), line by line, so a full-screen image costs no flash and
 *  only a few hundred bytes of RAM while it is being blitted.
 ******************************************************************************
 */
#ifndef __IMAGE_SD_H__
#define __IMAGE_SD_H__

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Directory holding every image on the card (FatFs path). */
#define IMAGE_SD_DIR        "0:/IMG"

/** Wallpaper file, whole-screen size (800x480 on this board). */
#define IMAGE_SD_BG_PATH    "S:/IMG/bg.bin"

/**
 * @brief  Probe the image folder and log what was found.
 *         Call once the volume is mounted and after lv_init().
 * @retval 0 ready (the folder exists), -1 no card / no folder
 */
int image_sd_init(void);

/** @retval 1 when the image folder was found. */
int image_sd_ready(void);

/**
 * @brief  Create the wallpaper as the bottom-most object of @p parent.
 * @retval the image object, or NULL when BG.BIN is missing (caller falls back
 *         to a plain colour).
 */
lv_obj_t *image_sd_bg(lv_obj_t *parent);

/**
 * @brief  Create one icon image from the card.
 * @param  name  file name without the folder and extension (e.g. "LOGO")
 * @retval the image object, or NULL when the file is missing
 */
lv_obj_t *image_sd_icon(lv_obj_t *parent, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __IMAGE_SD_H__ */
