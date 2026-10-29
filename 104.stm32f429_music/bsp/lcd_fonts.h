/**
 ******************************************************************************
 * @file    lcd_fonts.h
 * @brief   Bitmap font tables compiled into the image (ASCII + small CJK set).
 *
 *          The GBK glyphs used by LVGL come from the SD card; these tables are
 *          the compile-time fallback used when no card is present.
 ******************************************************************************
 */
#ifndef LCD_FONTS_H
#define LCD_FONTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One bitmap font: glyph table + geometry. */
typedef struct
{
    const uint8_t *table;      /* glyph bitmap data */
    uint16_t       width;      /* glyph cell width in pixels */
    uint16_t       height;     /* glyph cell height in pixels */
    uint16_t       sizes;      /* bytes per glyph */
    uint16_t       table_rows; /* glyph count */
} FONT_T;

/* Chinese (GBK) fonts */
extern FONT_T ch_font12;
extern FONT_T ch_font16;
extern FONT_T ch_font20;
extern FONT_T ch_font24;
extern FONT_T ch_font32;

/* ASCII fonts */
extern FONT_T ascii_font32;
extern FONT_T ascii_font24;
extern FONT_T ascii_font20;
extern FONT_T ascii_font16;
extern FONT_T ascii_font12;

#ifdef __cplusplus
}
#endif

#endif /* LCD_FONTS_H */
