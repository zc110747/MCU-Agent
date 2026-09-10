/**
  ******************************************************************************
  * @file    ui_font.h
  * @brief   UI text fonts: built-in Montserrat for ASCII with GBK fallback.
  *
  *  ui_font_12/16/24/32 are runtime copies of the LVGL built-in Montserrat
  *  fonts (compiled into flash, LV_FONT_MONTSERRAT_xx = 1) with their
  *  'fallback' pointer chained to the matching lv_font_gbk_xx SD-card font.
  *
  *  Effect: English letters, digits and symbols render in Montserrat; any
  *  character Montserrat does not have (Chinese) falls through to the GBK
  *  font, so mixed strings keep a single label and just work.
  *
  *  Exception: the TXT reader body must NOT use these - it keeps the plain
  *  &lv_font_gbk_16 so book text stays in one uniform typeface.
  ******************************************************************************
  */
#ifndef __UI_FONT_H
#define __UI_FONT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_font_t ui_font_12;
extern lv_font_t ui_font_16;
extern lv_font_t ui_font_24;
extern lv_font_t ui_font_32;

/**
  * @brief  Populate the wrapper fonts (idempotent, call once before use).
  * @note   Purely local struct copies - safe to call before or after lv_init()
  *         and regardless of whether the SD/GBK fonts are ready.
  */
void ui_font_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_FONT_H */
