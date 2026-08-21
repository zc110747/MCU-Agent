/**
  ******************************************************************************
  * @file    drv_oled_text.h
  * @brief   GBK Chinese font access: reads the glyph bitmaps straight off the
  *          SD card (1:/SYSTEM/FONT/) instead of burning flash on a font table.
  ******************************************************************************
  */
#ifndef _DRV_OLED_TEXT_H
#define _DRV_OLED_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "drv_oled_fonts.h"

/* Font files on the SD card (logical drive "1:"). */
extern pFONT CH_TEXT_Font12;
extern pFONT CH_TEXT_Font16;
extern pFONT CH_TEXT_Font24;
extern pFONT CH_TEXT_Font32;

/* Bit mask returned by lcd_driver_font_status() */
#define FONT_MASK_UNIGBK  (1U << 0)
#define FONT_MASK_GBK12   (1U << 1)
#define FONT_MASK_GBK16   (1U << 2)
#define FONT_MASK_GBK24   (1U << 3)
#define FONT_MASK_GBK32   (1U << 4)

/**
  * @brief  Mount the SD card and open the font files.
  * @retval RT_OK when at least one GBKxx.FON could be opened.
  */
GlobalType_t lcd_driver_font_init(void);

/**
  * @brief  Which font files are currently usable (FONT_MASK_* bit field).
  */
uint32_t lcd_driver_font_status(void);

/**
  * @brief  Fetch one GBK glyph, converted to LSB + row scan (OLED blitter fmt).
  * @param  code    pointer to the 2 byte GBK code
  * @param  pbuffer destination bitmap
  * @param  font    font descriptor (selects the file by Height)
  */
GlobalType_t lcd_driver_get_hzmat(uint8_t *code, uint8_t *pbuffer, pFONT *font);

/**
  * @brief  Fetch one GBK glyph *without* any bit reordering (MSB + column scan,
  *         exactly as stored in the .FON file).  LVGL needs MSB + row scan, so
  *         the caller transposes this into LVGL's format itself.
  * @param  code    pointer to the 2 byte GBK code (lead byte first)
  * @param  pbuffer destination, must hold at least font->Sizes bytes
  * @param  font    font descriptor (selects the file by Height)
  */
GlobalType_t lcd_driver_get_hzmat_raw(const uint8_t *code, uint8_t *pbuffer, const pFONT *font);

/**
  * @brief  Map a Unicode codepoint to a GBK code using 1:/SYSTEM/FONT/UNIGBK.BIN.
  *         The file is a flat array of 4-byte records sorted ascending by
  *         Unicode: [unicode_lo, unicode_hi, gbk_lo, gbk_hi] (little-endian),
  *         so a binary search is used.  Used by the LVGL GBK font bridge to
  *         turn the UTF-8 text LVGL decodes into the GBK code the .FON files
  *         are indexed by.
  * @param  unicode  the codepoint (e.g. 0x4E2D for '中')
  * @param  gbk_out  2-byte buffer, filled with lead/trail GBK bytes on success
  * @retval RT_OK if a mapping was found, RT_FAIL otherwise (or file absent).
  */
GlobalType_t lcd_driver_unigbk_lookup(uint32_t unicode, uint8_t *gbk_out);

#ifdef __cplusplus
}
#endif

#endif /* _DRV_OLED_TEXT_H */
