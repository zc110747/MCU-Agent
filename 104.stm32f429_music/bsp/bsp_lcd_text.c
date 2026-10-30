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
 *  first, row scan, so every glyph goes through convert_font_msb_column_to_lsb_row().
 ******************************************************************************
 */
#include "bsp_lcd_text.h"
#include "ff.h"
#include "fs_diskio.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "log.h"

#define FONT_NUMS 5
#define FONT_MAX_BYTES 192 /* 32x32 needs 128, leave headroom */

/* Index into font_name[] / fil_list[] */
#define FONT_IDX_UNIGBK 0
#define FONT_IDX_GBK12 1
#define FONT_IDX_GBK16 2
#define FONT_IDX_GBK24 3
#define FONT_IDX_GBK32 4

typedef struct
{
    FATFS   fs;                  /* volume 1: work area          */
    FIL     fil_list[FONT_NUMS]; /* kept open for the whole run  */
    uint8_t fil_valid[FONT_NUMS];
    uint8_t mounted;
} LCD_FS_INFO_T;

/* Everything the font loader owns: the volume it opened + the handles + name. */
typedef struct
{
    LCD_FS_INFO_T fs_info;
    char          volume[4]; /* "0:" / "1:" / "" */
} FONT_STATE_T;

static FONT_STATE_T s_font = {0};

/* File basenames, appended to whichever volume the loader picked
 * ("0:" = microSD). */
static const char *font_name[FONT_NUMS] = {
    "/SYSTEM/FONT/UNIGBK.BIN",
    "/SYSTEM/FONT/GBK12.FON",
    "/SYSTEM/FONT/GBK16.FON",
    "/SYSTEM/FONT/GBK24.FON",
    "/SYSTEM/FONT/GBK32.FON",
};

/* Scratch buffer for the raw (unconverted) glyph */
static uint8_t s_read_buffer[FONT_MAX_BYTES];

const char *lcd_driver_font_source(void)
{
    return s_font.volume;
}

/**
 * @brief  Close any previously opened font files before (re)opening them on
 *         another volume.  Without this a volume switch would leak the old
 *         FIL objects.
 */
static void font_close_all(void)
{
    uint8_t index;

    fs_lock();
    for (index = 0; index < FONT_NUMS; index++)
    {
        if (s_font.fs_info.fil_valid[index] != 0u)
        {
            (void)f_close(&s_font.fs_info.fil_list[index]);
        }
    }
    fs_unlock();

    memset(&s_font.fs_info, 0, sizeof(s_font.fs_info));
}

/**
 * @brief  Open every font file we know about on the given volume.
 *
 * @param  vol  FatFs volume prefix, either FS_VOL_SD ("1:") or
 *              FS_VOL_USB ("0:").  The volume must already be mounted by its
 *              owner (app/sd_card.c for the card, usb_host_app.c for the
 *              U-disk): calling f_mount() here would reassign the drive's
 *              FATFS work area while the owner may still be using it.
 *
 * @retval RT_OK if at least one GBKxx.FON is available.
 */
GLOBAL_TYPE_T lcd_driver_font_init(const char *vol)
{
    FRESULT res;
    uint8_t index;
    char    path[48];

    if (vol == NULL)
    {
        return RT_FAIL;
    }

    font_close_all();

    s_font.volume[0] = '\0';

    /* The volume is already mounted by its owner, so we only open files here,
     * serialized by the shared FS lock. */
    fs_lock();
    for (index = 0; index < FONT_NUMS; index++)
    {
        (void)snprintf(path, sizeof(path), "%s%s", vol, font_name[index]);
        res = f_open(&s_font.fs_info.fil_list[index], path, FA_READ);
        if (res == FR_OK)
        {
            s_font.fs_info.fil_valid[index] = 1;
        }
        else
        {
            PRINT_LOG("[FONT] open %s failed rc=%d\r\n", path, (int32_t)res);
        }
    }
    fs_unlock();

    /* UNIGBK.BIN alone is useless - we need at least one glyph file */
    if (s_font.fs_info.fil_valid[FONT_IDX_GBK12] ||
        s_font.fs_info.fil_valid[FONT_IDX_GBK16] ||
        s_font.fs_info.fil_valid[FONT_IDX_GBK24] ||
        s_font.fs_info.fil_valid[FONT_IDX_GBK32])
    {
        s_font.fs_info.mounted = 1U;
        (void)snprintf(s_font.volume, sizeof(s_font.volume), "%s", vol);
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
        if (s_font.fs_info.fil_valid[i])
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
static void convert_font_msb_column_to_lsb_row(const uint8_t *src, uint8_t *dst,
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
    case 12:
        return FONT_IDX_GBK12;
    case 16:
        return FONT_IDX_GBK16;
    case 24:
        return FONT_IDX_GBK24;
    case 32:
        return FONT_IDX_GBK32;
    default:
        return 0xFF;
    }
}

/**
 * @brief  Seek to a glyph and read it verbatim from the .FON file.
 * @param  code  2 byte GBK code, lead byte first
 * @param  dst   destination, at least font->sizes bytes
 * @note   No bit reordering: the data stays MSB first / column scan.
 */
static GLOBAL_TYPE_T font_read_raw(const uint8_t *code, uint8_t *dst, const FONT_T *font)
{
    uint8_t  qh, ql;
    uint32_t foffset;
    UINT     bytes_read;
    FRESULT  res;
    uint8_t  file_index;

    /* Guard against a descriptor that would overrun the scratch buffer */
    if (font->sizes == 0 || font->sizes > FONT_MAX_BYTES)
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
    foffset = ((uint32_t)190 * qh + ql) * font->sizes;

    file_index = font_index_from_height(font->height);
    if (file_index == 0xFF || !s_font.fs_info.fil_valid[file_index])
    {
        return RT_FAIL;
    }

    /* While the USB host owns the card, fs_lock() would block until the whole
     * transfer session ends.  A missing glyph is a cosmetic gap; freezing the
     * UI task for minutes is not, so fail fast instead. */
    if (fs_usb_session_active() != 0)
    {
        return RT_FAIL;
    }

    if (fs_try_lock(20U) == 0)
    {
        return RT_FAIL;
    }

    res = f_lseek(&s_font.fs_info.fil_list[file_index], foffset);
    if (res == FR_OK)
    {
        res = f_read(&s_font.fs_info.fil_list[file_index], dst, font->sizes, &bytes_read);
    }
    fs_unlock();
    if (res != FR_OK || bytes_read != font->sizes)
    {
        return RT_FAIL;
    }

    return RT_OK;
}

/**
 * @brief  Blank a glyph buffer so a read failure shows up as a gap, never as
 *         garbage left over from the previous character.
 */
static void font_blank(uint8_t *pbuffer, const FONT_T *font)
{
    uint16_t n = font->sizes;

    if (n == 0 || n > FONT_MAX_BYTES)
    {
        n = FONT_MAX_BYTES;
    }
    memset(pbuffer, 0, n);
}

/**
 * @brief  Read one GBK glyph into pbuffer (LSB, row scan).
 */
GLOBAL_TYPE_T lcd_driver_get_hzmat(uint8_t *code, uint8_t *pbuffer, FONT_T *font)
{
    if (code == NULL || pbuffer == NULL || font == NULL)
    {
        return RT_FAIL;
    }

    if (font_read_raw(code, s_read_buffer, font) != RT_OK)
    {
        font_blank(pbuffer, font);
        return RT_FAIL;
    }

    convert_font_msb_column_to_lsb_row(s_read_buffer, pbuffer,
                                       font->width, font->height);
    return RT_OK;
}

/**
 * @brief  Read one GBK glyph without touching the bit order (see header).
 */
GLOBAL_TYPE_T lcd_driver_get_hzmat_raw(const uint8_t *code, uint8_t *pbuffer, const FONT_T *font)
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
FONT_T ch_text_font12 = {NULL, 12, 12, 24, 0};
FONT_T ch_text_font16 = {NULL, 16, 16, 32, 0};
FONT_T ch_text_font24 = {NULL, 24, 24, 72, 0};
FONT_T ch_text_font32 = {NULL, 32, 32, 128, 0};
