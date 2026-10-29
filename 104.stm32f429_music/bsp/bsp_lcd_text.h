/**
  ******************************************************************************
 * @file    drv_oled_text.h
 * @brief   GBK Chinese font access: reads the glyph bitmaps straight off a
 *          removable volume (<vol>/SYSTEM/FONT/) instead of burning flash on a
 *          font table.  <vol> is "1:" (microSD) or "0:" (USB MSC); the boot
 *          loader tries the card first and falls back to USB.
  ******************************************************************************
  */
#ifndef _BSP_LCD_TEXT_H
#define _BSP_LCD_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lcd_fonts.h"

/* Shared status/result type (mirrors 003 Core/Inc/main.h).  Guarded so that
 * including both bsp_lcd.h and bsp_lcd_text.h in one TU is safe. */
#ifndef GLOBAL_TYPE_T_DEFINED
#define GLOBAL_TYPE_T_DEFINED
typedef enum
{
    RT_OK = 0,
    RT_FAIL,
} GlobalType_t;
#endif

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
  * @brief  Open the font files on the given (already mounted) volume.
  *
  * @param  vol  FatFs volume prefix: "1:" for the microSD card, "0:" for the
  *              USB mass-storage device.  The volume must be mounted by its
  *              owner first; this function never calls f_mount().
  *
  * @retval RT_OK when at least one GBKxx.FON could be opened.
  */
GlobalType_t lcd_driver_font_init(const char *vol);

/**
  * @brief  Which volume the currently open fonts were loaded from
  *         ("1:" = microSD, "0:" = USB, "" = none).
  */
const char *lcd_driver_font_source(void);

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

#endif /* _BSP_LCD_TEXT_H */
