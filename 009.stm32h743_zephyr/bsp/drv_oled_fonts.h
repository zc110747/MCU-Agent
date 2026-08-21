/**
  ******************************************************************************
  * @file    drv_oled_fonts.h
  * @brief   Font descriptor types for the SD-card backed GBK font reader.
  *
  *  This is a trimmed, Zephyr-friendly version of the original bare-metal
  *  drv_oled_fonts.h.  The compiled-in ASCII/Chinese tables are not needed
  *  here because LVGL supplies the ASCII font and the Chinese glyphs are read
  *  straight from the .FON files on the SD card.
  ******************************************************************************
  */
#ifndef __OLED_FONTS_H
#define __OLED_FONTS_H

#include <stdint.h>

/* Generic return code used across the ported drivers (was in the old main.h). */
typedef int GlobalType_t;
#define RT_OK    0
#define RT_FAIL -1

/* Font-related structure definition (kept identical to the bare-metal API so
 * drv_oled_text.c can be reused with minimal changes). */
typedef struct
{
    const uint8_t  *pTable;    /* glyph bitmap pointer; NULL => read from SD  */
    uint16_t        Width;     /* width  of a single glyph in pixels          */
    uint16_t        Height;    /* height of a single glyph in pixels          */
    uint16_t        Sizes;     /* number of bytes per glyph bitmap            */
    uint16_t        Table_Rows;/* rows of the 2D table (unused for SD fonts) */
} pFONT;

/* SD-card resident GBK font descriptors (pTable == NULL => read from file). */
extern pFONT CH_TEXT_Font12;
extern pFONT CH_TEXT_Font16;
extern pFONT CH_TEXT_Font24;
extern pFONT CH_TEXT_Font32;

#endif /* __OLED_FONTS_H */
