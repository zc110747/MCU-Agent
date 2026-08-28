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

#include "main.h"
#include "drv_oled_fonts.h"

/*
 * SD card resident fonts.  pTable is NULL, which is the flag the display code
 * uses to decide "fetch this glyph from the file system" instead of "look it
 * up in the compiled-in table".
 */
extern pFONT CH_TEXT_Font12;
extern pFONT CH_TEXT_Font16;
extern pFONT CH_TEXT_Font24;
extern pFONT CH_TEXT_Font32;

/* Bit mask returned by lcd_driver_font_status() */
#define FONT_MASK_UNIGBK    (1U << 0)
#define FONT_MASK_GBK12     (1U << 1)
#define FONT_MASK_GBK16     (1U << 2)
#define FONT_MASK_GBK24     (1U << 3)
#define FONT_MASK_GBK32     (1U << 4)

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
  * @brief  Fetch one GBK glyph.
  * @param  code    pointer to the 2 byte GBK code
  * @param  pbuffer destination bitmap, LSB + row scan
  * @param  font    font descriptor (selects the file by Height)
  */
GlobalType_t lcd_driver_get_hzmat(uint8_t *code, uint8_t *pbuffer, pFONT *font);

/**
  * @brief  Fetch one GBK glyph *without* any bit reordering.
  *
  *  Returns the bytes exactly as they sit in the GBKxx.FON file, i.e. MSB
  *  first, column scan, (Height+7)/8 bytes per column.  lcd_driver_get_hzmat()
  *  transposes that into the LSB + row-scan layout the OLED blitter wants;
  *  LVGL however needs MSB + continuous row bitstream, so it takes the raw data
  *  and does its own single-pass conversion instead of transposing twice.
  *
  * @param  code    pointer to the 2 byte GBK code (lead byte first)
  * @param  pbuffer destination, must hold at least font->Sizes bytes
  * @param  font    font descriptor (selects the file by Height)
  */
GlobalType_t lcd_driver_get_hzmat_raw(const uint8_t *code, uint8_t *pbuffer, const pFONT *font);

#ifdef __cplusplus
}
#endif

#endif /* _DRV_OLED_TEXT_H */
