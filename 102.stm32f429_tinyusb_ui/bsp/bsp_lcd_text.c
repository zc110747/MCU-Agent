/**
  ******************************************************************************
  * @file    drv_oled_text.c
  * @brief   GBK Chinese font reader (SD card backed).
  *
  *  Layout of the .FON files
  *  ------------------------
  *  Standard GBK "dot matrix" files: glyphs are stored in code order, two byte
  *  GBK code (qh, ql) mapped to a linear index
  *
  *      index  = 190 * (qh - 0x81) + (ql - 0x40 or 0x41)
  *      offset = index * bytes_per_glyph
  *
  *  The stored bitmap is MSB first, column scan.  The display driver wants LSB
  *  first, row scan, so every glyph goes through Convert_Font_MSB_Column_to_LSB_Row().
  ******************************************************************************
  */
#include "bsp_lcd_text.h"
#include "ff.h"
#include <stddef.h>
#include <string.h>

#define FONT_NUMS       5
#define FONT_MAX_BYTES  192     /* 32x32 needs 128, leave headroom */

/* Index into font_name[] / fil_list[] */
#define FONT_IDX_UNIGBK 0
#define FONT_IDX_GBK12  1
#define FONT_IDX_GBK16  2
#define FONT_IDX_GBK24  3
#define FONT_IDX_GBK32  4

typedef struct
{
    FATFS   fs;                     /* volume 1: work area          */
    FIL     fil_list[FONT_NUMS];    /* kept open for the whole run  */
    uint8_t fil_valid[FONT_NUMS];
    uint8_t mounted;
} LCD_FS_INFO;

static LCD_FS_INFO g_lcd_fs_info;

static const char *font_name[FONT_NUMS] = {
    "0:/SYSTEM/FONT/UNIGBK.BIN",
    "0:/SYSTEM/FONT/GBK12.FON",
    "0:/SYSTEM/FONT/GBK16.FON",
    "0:/SYSTEM/FONT/GBK24.FON",
    "0:/SYSTEM/FONT/GBK32.FON",
};

/* Scratch buffer for the raw (unconverted) glyph */
static uint8_t read_buffer[FONT_MAX_BYTES];

/**
  * @brief  Mount volume 1 (SD) and open every font file we know about.
  * @retval RT_OK if at least one GBKxx.FON is available.
  */
GlobalType_t lcd_driver_font_init(void)
{
    FRESULT res;
    uint8_t index;

    memset(&g_lcd_fs_info, 0, sizeof(g_lcd_fs_info));

    /* Mount immediately (opt = 1) so a missing card is reported here */
    res = f_mount(&g_lcd_fs_info.fs, "0:", 1);
    if (res != FR_OK)
    {
        return RT_FAIL;
    }
    g_lcd_fs_info.mounted = 1;

    for (index = 0; index < FONT_NUMS; index++)
    {
        res = f_open(&g_lcd_fs_info.fil_list[index], font_name[index], FA_READ);
        if (res == FR_OK)
        {
            g_lcd_fs_info.fil_valid[index] = 1;
        }
    }

    /* UNIGBK.BIN alone is useless - we need at least one glyph file */
    if (g_lcd_fs_info.fil_valid[FONT_IDX_GBK12] ||
        g_lcd_fs_info.fil_valid[FONT_IDX_GBK16] ||
        g_lcd_fs_info.fil_valid[FONT_IDX_GBK24] ||
        g_lcd_fs_info.fil_valid[FONT_IDX_GBK32])
    {
        return RT_OK;
    }

    return RT_FAIL;
}

uint32_t lcd_driver_font_status(void)
{
    uint32_t mask = 0;
    uint8_t  i;

    for (i = 0; i < FONT_NUMS; i++)
    {
        if (g_lcd_fs_info.fil_valid[i])
        {
            mask |= (1U << i);
        }
    }
    return mask;
}

/**
  * @brief  Transpose a glyph from MSB + column scan to LSB + row scan.
  * @param  src    source bitmap (MSB, column scan)
  * @param  dst    destination bitmap (LSB, row scan)
  * @param  width  glyph width in pixels
  * @param  height glyph height in pixels
  */
static void Convert_Font_MSB_Column_to_LSB_Row(const uint8_t *src, uint8_t *dst,
                                               uint16_t width, uint16_t height)
{
    uint16_t bytes_per_col = (uint16_t)((height + 7) / 8);
    uint16_t bytes_per_row = (uint16_t)((width + 7) / 8);

    memset(dst, 0, (size_t)bytes_per_row * height);

    for (uint16_t col = 0; col < width; col++)
    {
        for (uint16_t byte_idx = 0; byte_idx < bytes_per_col; byte_idx++)
        {
            uint8_t src_byte = src[col * bytes_per_col + byte_idx];

            for (uint8_t bit = 0; bit < 8; bit++)
            {
                uint16_t row = (uint16_t)(byte_idx * 8 + bit);
                if (row >= height)
                {
                    break;
                }

                if (src_byte & (0x80 >> bit))
                {
                    dst[row * bytes_per_row + col / 8] |= (uint8_t)(1 << (col % 8));
                }
            }
        }
    }
}

/**
  * @brief  Map a font height to the file that holds it.
  * @retval index into fil_list[], or 0xFF when unsupported.
  */
static uint8_t font_index_from_height(uint16_t height)
{
    switch (height)
    {
        case 12: return FONT_IDX_GBK12;
        case 16: return FONT_IDX_GBK16;
        case 24: return FONT_IDX_GBK24;
        case 32: return FONT_IDX_GBK32;
        default: return 0xFF;
    }
}

/**
  * @brief  Seek to a glyph and read it verbatim from the .FON file.
  * @param  code  2 byte GBK code, lead byte first
  * @param  dst   destination, at least font->Sizes bytes
  * @note   No bit reordering: the data stays MSB first / column scan.
  */
static GlobalType_t font_read_raw(const uint8_t *code, uint8_t *dst, const pFONT *font)
{
    uint8_t  qh, ql;
    uint32_t foffset;
    UINT     bytes_read;
    FRESULT  res;
    uint8_t  file_index;

    /* Guard against a descriptor that would overrun the scratch buffer */
    if (font->Sizes == 0 || font->Sizes > FONT_MAX_BYTES)
    {
        return RT_FAIL;
    }

    qh = code[0];
    ql = code[1];

    /* Not a valid GBK lead/trail byte pair */
    if (qh < 0x81 || qh == 0xFF || ql < 0x40 || ql == 0xFF)
    {
        return RT_FAIL;
    }

    /* 0x7F is not used as a trail byte, the table skips it */
    ql -= (ql < 0x7F) ? 0x40 : 0x41;
    qh -= 0x81;
    foffset = ((uint32_t)190 * qh + ql) * font->Sizes;

    file_index = font_index_from_height(font->Height);
    if (file_index == 0xFF || !g_lcd_fs_info.fil_valid[file_index])
    {
        return RT_FAIL;
    }

    res = f_lseek(&g_lcd_fs_info.fil_list[file_index], foffset);
    if (res != FR_OK)
    {
        return RT_FAIL;
    }

    res = f_read(&g_lcd_fs_info.fil_list[file_index], dst, font->Sizes, &bytes_read);
    if (res != FR_OK || bytes_read != font->Sizes)
    {
        return RT_FAIL;
    }

    return RT_OK;
}

/**
  * @brief  Blank a glyph buffer so a read failure shows up as a gap, never as
  *         garbage left over from the previous character.
  */
static void font_blank(uint8_t *pbuffer, const pFONT *font)
{
    uint16_t n = font->Sizes;

    if (n == 0 || n > FONT_MAX_BYTES)
    {
        n = FONT_MAX_BYTES;
    }
    memset(pbuffer, 0, n);
}

/**
  * @brief  Read one GBK glyph into pbuffer (LSB, row scan).
  */
GlobalType_t lcd_driver_get_hzmat(uint8_t *code, uint8_t *pbuffer, pFONT *font)
{
    if (code == NULL || pbuffer == NULL || font == NULL)
    {
        return RT_FAIL;
    }

    if (font_read_raw(code, read_buffer, font) != RT_OK)
    {
        font_blank(pbuffer, font);
        return RT_FAIL;
    }

    Convert_Font_MSB_Column_to_LSB_Row(read_buffer, pbuffer,
                                       font->Width, font->Height);
    return RT_OK;
}

/**
  * @brief  Read one GBK glyph without touching the bit order (see header).
  */
GlobalType_t lcd_driver_get_hzmat_raw(const uint8_t *code, uint8_t *pbuffer, const pFONT *font)
{
    if (code == NULL || pbuffer == NULL || font == NULL)
    {
        return RT_FAIL;
    }

    if (font_read_raw(code, pbuffer, font) != RT_OK)
    {
        font_blank(pbuffer, font);
        return RT_FAIL;
    }

    return RT_OK;
}

/* ---------------------------------------------------------------------------
 * SD card resident font descriptors (pTable == NULL -> read from file)
 *   Sizes = width/8 rounded up * height
 * -------------------------------------------------------------------------*/
pFONT CH_TEXT_Font12 = { NULL, 12, 12,  24, 0 };
pFONT CH_TEXT_Font16 = { NULL, 16, 16,  32, 0 };
pFONT CH_TEXT_Font24 = { NULL, 24, 24,  72, 0 };
pFONT CH_TEXT_Font32 = { NULL, 32, 32, 128, 0 };
