/**
  ******************************************************************************
  * @file    lv_port_fs.h
  * @brief   LVGL file system driver on top of FatFs, with a read block cache.
  *
  *  Why not LV_USE_FS_FATFS
  *  -----------------------
  *  LVGL ships its own FatFs driver (lv_fs_fatfs.c) and it does have a cache
  *  knob (LV_FS_FATFS_CACHE_SIZE), but that cache is a *single* forward window
  *  per file: every backwards seek invalidates it.  stb_truetype - the rasteriser
  *  behind the HarmonyOS engine - does not stream a font sequentially, it jumps
  *  between four or five table regions (cmap / loca / glyf / hmtx / kern) and,
  *  in stream mode, reads them one byte at a time.  A single window therefore
  *  thrashes on almost every access.
  *
  *  This driver instead keeps a small pool of 512 B blocks shared by all open
  *  files, tagged with (file id, block number) and replaced least-recently-used.
  *  A glyph that used to cost ~150 SD transactions (seek + 1 byte read each)
  *  now costs one block miss per table region.
  *
  *  Two more things LVGL's driver does not do for us here:
  *
  *  - lv_fs_get_real_path() strips the "1:" prefix before calling us, and FatFs
  *    would then resolve the path against logical drive 0 (which is not
  *    mounted).  The prefix is put back in fs_open().
  *  - The cache is shared, so four font sizes streaming the same .ttf do not
  *    need four private buffers.
  ******************************************************************************
  */
#ifndef __LV_PORT_FS_H
#define __LV_PORT_FS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Register the cached FatFs driver on drive '1' with LVGL.
  * @note   FatFs must already be mounted on "1:" (see lcd_driver_font_init()).
  */
void lv_port_fs_init(void);

/**
  * @brief  Block cache counters, for tuning LV_PORT_FS_BLOCK_COUNT.
  * @param  hits    reads served from RAM        (may be NULL)
  * @param  misses  reads that reached the card  (may be NULL)
  */
void lv_port_fs_stats(uint32_t *hits, uint32_t *misses);

#ifdef __cplusplus
}
#endif

#endif /* __LV_PORT_FS_H */
