/**
  ******************************************************************************
  * @file    ui_font.c
  * @brief   UI text fonts: built-in Montserrat for ASCII with GBK fallback.
  *
  *  Each wrapper is a RAM copy of the const built-in Montserrat font of the
  *  same size; only the 'fallback' member is changed to point at the GBK
  *  font.  LVGL's lv_font_get_glyph_dsc() resolves missing glyphs through
  *  the fallback chain recursively, so Chinese characters render from the
  *  SD-card GBK font exactly as before while ASCII switches to Montserrat.
  ******************************************************************************
  */
#include "ui_font.h"
#include "lv_font_gbk.h"

/*---------------------------------------------------------------------------
 *  Wrappers (populated by ui_font_init)
 *--------------------------------------------------------------------------*/
lv_font_t ui_font_12;
lv_font_t ui_font_16;
lv_font_t ui_font_24;
lv_font_t ui_font_32;

void ui_font_init(void)
{
    static uint8_t s_done = 0U;

    if (s_done != 0U)
    {
        return;
    }

    ui_font_12          = lv_font_montserrat_12;
    ui_font_12.fallback = &lv_font_gbk_12;

    ui_font_16          = lv_font_montserrat_16;
    ui_font_16.fallback = &lv_font_gbk_16;

    ui_font_24          = lv_font_montserrat_24;
    ui_font_24.fallback = &lv_font_gbk_24;

    ui_font_32          = lv_font_montserrat_32;
    ui_font_32.fallback = &lv_font_gbk_32;

    s_done = 1U;
}
