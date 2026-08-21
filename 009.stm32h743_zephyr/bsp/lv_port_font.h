/**
  ******************************************************************************
  * @file    lv_port_font.h
  * @brief   LVGL v8 custom font that streams Chinese (GBK) glyphs from the SD
  *          card instead of compiling the font into flash.
  *
  *  One lv_font_t instance is created per dot size (12/16/24/32).  Each carries
  *  the matching pFONT* in its `dsc` field so the glyph callbacks know which
  *  GBKxx.FON file (and pixel size) to read.  ASCII / Latin-1 codepoints are
  *  intentionally NOT served here - they fall through to the Montserrat
  *  fallback font so numbers and punctuation stay crisp.
  ******************************************************************************
  */
#ifndef _LV_PORT_FONT_H
#define _LV_PORT_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

/* SD-card backed Chinese fonts (read on demand, 1bpp, MSB-first row scan). */
extern lv_font_t gbk_font_12;
extern lv_font_t gbk_font_16;
extern lv_font_t gbk_font_24;
extern lv_font_t gbk_font_32;

/**
  * @brief  Bind the pFONT descriptors and attach the Montserrat fallback font.
  *         Must be called once after lcd_driver_font_init() (so the .FON files
  *         are open) and before any LVGL widget uses these fonts.
  */
void lv_port_font_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _LV_PORT_FONT_H */
