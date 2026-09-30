/**
  ******************************************************************************
  * @file    lv_font_provider.h
  * @brief   One place that decides which CJK font engine the UI is served from.
  *
  *  The UI never names lv_font_gbk_16 / a HarmonyOS font directly - it asks
  *  lv_font_provider_get(16) and gets whatever engine is configured and, more
  *  importantly, whatever actually managed to load (no card, no .ttf, ...).
  ******************************************************************************
  */
#ifndef __LV_FONT_PROVIDER_H
#define __LV_FONT_PROVIDER_H

#include "lvgl.h"
#include "main.h"
#include "lv_font_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FONT_ENGINE_GBK = 0,
    FONT_ENGINE_HARMONYOS,
    FONT_ENGINE_CTF
} FontEngine_t;

/**
  * @brief  Probe the SD card for a .ctf and the .ttf it belongs to.
  *
  *  Tries CTF_FONT_DIR/CTF_FONT_NAME.ctf first, then the first .ctf found in
  *  CTF_FONT_DIR, deriving the .ttf name by swapping the extension.  Both
  *  buffers must be at least 128 bytes.
  *
  * @retval RT_OK when a pair was found and the pair really is a pair
  */
GlobalType_t lv_font_provider_locate(char *ctf_path,
                                     char *ttf_path,
                                     uint32_t path_size);

/**
  * @brief  Bring up the configured engine.
  *
  *  Must run after lv_port_fs_init() (the HarmonyOS engine streams the .ttf
  *  through LVGL's file system) and after the SD card has been mounted.
  *
  * @retval RT_OK when the requested engine came up, RT_FAIL when it fell back.
  */
GlobalType_t lv_font_provider_init(void);

/**
  * @brief  Engine actually in use, which may differ from LV_FONT_ENGINE.
  */
FontEngine_t lv_font_provider_engine(void);

/**
  * @brief  Short name of the engine in use, for the UI / log ("鸿蒙TTF"/"GBK").
  */
const char *lv_font_provider_name(void);

/**
  * @brief  Font for a pixel size, never NULL.
  * @param  size  12, 16, 24 or 32.  Unknown sizes fall back to 16.
  */
const lv_font_t *lv_font_provider_get(uint16_t size);

/**
  * @brief  Default font handed to widgets that carry no text style of their own.
  */
const lv_font_t *lv_font_provider_default(void);

/**
  * @brief  Pixel size a CTF/TTF font was built at, or 0 for non-TTF fonts.
  *         Lets the UI decide whether a label should be glyph-preloaded.
  */
uint16_t lv_font_provider_px_of(const lv_font_t *f);

/**
  * @brief  Notify the font layer that a new page became visible, so it can
  *         re-pin the current page's glyphs and let the previous page's fall
  *         out of the cache under LRU pressure.
  */
void lv_font_provider_on_page_shown(void);

/**
  * @brief  Preload the text of an LVGL label (TTF engines only; ignored otherwise).
  */
void lv_font_provider_preload_label(const lv_obj_t *lbl);

/**
  * @brief  Glyphs still queued for asynchronous rasterisation.
  * @retval 0 for non-TTF engines (GBK / HarmonyOS), or when the queue drained.
  */
uint32_t lv_font_provider_preload_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* __LV_FONT_PROVIDER_H */
