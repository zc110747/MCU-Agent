/**
  ******************************************************************************
  * @file    ui_page_font.h
  * @brief   Font-engine status page (Chinese) - used to eyeball glyph latency.
  ******************************************************************************
  */
#ifndef __UI_PAGE_FONT_H
#define __UI_PAGE_FONT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the font-status screen (off-screen; caller loads it). */
lv_obj_t *ui_page_font_build(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_FONT_H */
