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
  *  The stored bitmap is MSB first, column scan.  lcd_driver_get_hzmat()
  *  transposes that into the LSB + row-scan layout the bare-metal OLED blitter
  *  wants; LVGL however needs MSB + continuous row bitstream, so
  *  lcd_driver_get_hzmat_raw() returns the bytes verbatim and the LVGL font
  *  bridge does its own single transpose.
  *
  *  Ported to Zephyr: only the FatFs include changed (now <ff.h> from the
  *  Zephyr FatFs module); all logic is unchanged.
  ******************************************************************************
  */
#include "drv_oled_text.h"
#include <ff.h>
#include <string.h>
#include <zephyr/sys/printk.h>

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

/* Logical drive is "SD:" to match CONFIG_SDMMC_VOLUME_NAME="SD" and the
 * FatFs string volume table (FF_VOLUME_STRS = ...,"SD",...). */
static const char *font_name[FONT_NUMS] = {
    "SD:/SYSTEM/FONT/UNIGBK.BIN",
    "SD:/SYSTEM/FONT/GBK12.FON",
    "SD:/SYSTEM/FONT/GBK16.FON",
    "SD:/SYSTEM/FONT/GBK24.FON",
    "SD:/SYSTEM/FONT/GBK32.FON",
};

/* Scratch buffer for the raw (unconverted) glyph */
static uint8_t read_buffer[FONT_MAX_BYTES];

/*
 * UNIGBK.BIN is a TWO-SEGMENT table:
 *   seg1 [0, seg1_count):  [uni_lo, uni_hi, gbk_lo, gbk_hi] sorted by
 *                          Unicode ascending - covers the symbols AND all
 *                          Han codepoints (0x4E2D -> D0D6 lives here).
 *   one  zero padding record
 *   seg2 [seg1_count+1, ..): [gbk_lo, gbk_hi, uni_lo, uni_hi] sorted by
 *                          GBK ascending.
 * A plain binary search over the whole file fails because the two segments
 * use different sort keys.  We only need Unicode -> GBK, so search seg1.
 * seg1_count is located once at init (first all-zero record) and cached.
 */
static uint32_t g_unigbk_seg1 = 0;   /* 0 = not scanned yet */

/**
  * @brief  Find where segment 1 ends (first all-zero record), cache it.
  * @param  f  open UNIGBK.BIN
  * @retval segment-1 record count (whole file if no zero record found)
  */
static uint32_t unigbk_find_seg1(FIL *f)
{
    uint8_t buf[2048];
    uint32_t total = (uint32_t)(f_size(f) / 4);
    uint32_t idx = 0;
    UINT br = 0;

    f_lseek(f, 0);
    while (idx < total)
    {
        uint32_t chunk = total - idx;
        if (chunk > 512)
        {
            chunk = 512;
        }
        if (f_read(f, buf, chunk * 4, &br) != FR_OK || br != chunk * 4)
        {
            break;
        }
        for (uint32_t i = 0; i < chunk; i++)
        {
            uint32_t u  = (uint32_t)buf[i * 4]     | ((uint32_t)buf[i * 4 + 1] << 8);
            uint32_t gk = (uint32_t)buf[i * 4 + 2] | ((uint32_t)buf[i * 4 + 3] << 8);

            if (u == 0 && gk == 0)
            {
                return idx + i;   /* first all-zero record = end of seg1 */
            }
        }
        idx += chunk;
    }
    return total;
}

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
    res = f_mount(&g_lcd_fs_info.fs, "SD:", 1);
    if (res != FR_OK)
    {
        printk("FONT: f_mount(\"SD:\") failed, FRESULT=%d\n", (int)res);
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
        else
        {
            printk("FONT: f_open(%s) failed, FRESULT=%d\n",
                   font_name[index], (int)res);
        }
    }

    /* Debug: dump the UNIGBK.BIN header so we can verify the record layout
     * (assumed [uni_lo, uni_hi, gbk_lo, gbk_hi], little-endian, sorted by
     * Unicode ascending).  Real-world UNIGBK.BIN files sometimes store
     * [gbk, uni] instead - the dump tells us which. */
    if (g_lcd_fs_info.fil_valid[FONT_IDX_UNIGBK])
    {
        printk("FONT: UNIGBK size=%u\n", (unsigned)f_size(&g_lcd_fs_info.fil_list[FONT_IDX_UNIGBK]));
    }

    /* UNIGBK.BIN alone is useless - we need at least one glyph file */
    if (g_lcd_fs_info.fil_valid[FONT_IDX_GBK12] ||
        g_lcd_fs_info.fil_valid[FONT_IDX_GBK16] ||
        g_lcd_fs_info.fil_valid[FONT_IDX_GBK24] ||
        g_lcd_fs_info.fil_valid[FONT_IDX_GBK32])
    {
        /* Self-test: verify the Unicode->GBK path used by the renderer. */
        {
            uint8_t  gbk[2];
            GlobalType_t r = lcd_driver_unigbk_lookup(0x4E2D, gbk);

            printk("FONT: seg1=%u\n", (unsigned)g_unigbk_seg1);
            printk("FONT: lookup 0x4E2D -> %s",
                   (r == RT_OK) ? "OK" : "FAIL");
            if (r == RT_OK)
            {
                printk(" (gbk %02X%02X)\n", gbk[0], gbk[1]);
            }
            else
            {
                printk("\n");
            }
        }
        /* Multi-char mapping check: what GBK does each UI codepoint map to? */
        {
            static const uint32_t t_uni[] = { 0x4E2D, 0x6587, 0x65F6, 0x949F,
                                              0x4E3B, 0x5B57, 0x9891, 0x53C8,
                                              0x7B14, 0x8FD0, 0x884C };
            static const char *t_tag[] = { "zhong", "wen",   "shi",  "zhong2",
                                           "zhu",   "zi",    "pin",  "you",
                                           "bi",    "yun",   "xing" };

            for (uint32_t i = 0; i < sizeof(t_uni) / sizeof(t_uni[0]); i++)
            {
                uint8_t gbk[2];

                printk("FONT: U+%04X %-6s -> ", (unsigned)t_uni[i], t_tag[i]);
                if (lcd_driver_unigbk_lookup(t_uni[i], gbk) == RT_OK)
                {
                    printk("OK gbk=%02X%02X\n", gbk[0], gbk[1]);
                }
                else
                {
                    printk("FAIL\n");
                }
            }
        }
        /* Read the "shi"(0xCAB1) 16x16 glyph straight from the .FON and dump
         * the first rows, so we can tell map-vs-file offset problems apart. */
        {
            uint8_t gbk16[2] = { 0xCA, 0xB1 };
            uint8_t raw[32];
            uint8_t i;

            if (lcd_driver_get_hzmat_raw(gbk16, raw, &CH_TEXT_Font16) == RT_OK)
            {
                printk("FONT: hz(CAB1)16:");
                for (i = 0; i < 8; i++)
                {
                    printk(" %02X", raw[i]);
                }
                printk("\n");
            }
            else
            {
                printk("FONT: hz(CAB1)16 READ FAIL\n");
            }
        }
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
  */
static GlobalType_t font_read_raw(const uint8_t *code, uint8_t *dst, const pFONT *font)
{
    uint8_t  qh, ql;
    uint32_t foffset;
    UINT     bytes_read;
    FRESULT  res;
    uint8_t  file_index;

    if (font->Sizes == 0 || font->Sizes > FONT_MAX_BYTES)
    {
        return RT_FAIL;
    }

    qh = code[0];
    ql = code[1];

    if (qh < 0x81 || qh == 0xFF || ql < 0x40 || ql == 0xFF)
    {
        return RT_FAIL;
    }

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
  * @brief  Blank a glyph buffer so a read failure shows up as a gap.
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

/**
  * @brief  Map a Unicode codepoint to a GBK code via UNIGBK.BIN (binary search).
  *         Record layout (both segments): 4 bytes little-endian, segment 1 is
  *         [uni_lo, uni_hi, gbk_lo, gbk_hi] - note the GBK field is also
  *         little-endian, so the caller receives (gbk_hi, gbk_lo) order.
  */
GlobalType_t lcd_driver_unigbk_lookup(uint32_t unicode, uint8_t *gbk_out)
{
    FIL    *f;
    FSIZE_t size;
    uint32_t count;
    uint32_t lo, hi;
    uint8_t  rec[4];
    UINT     br;

    if (gbk_out == NULL)
    {
        return RT_FAIL;
    }
    if (!g_lcd_fs_info.fil_valid[FONT_IDX_UNIGBK])
    {
        return RT_FAIL;   /* UNIGBK.BIN not open */
    }

    f = &g_lcd_fs_info.fil_list[FONT_IDX_UNIGBK];
    size = f_size(f);
    if (size == 0 || (size % 4) != 0)
    {
        return RT_FAIL;
    }

    if (g_unigbk_seg1 == 0)
    {
        g_unigbk_seg1 = unigbk_find_seg1(f);
    }

    count = g_unigbk_seg1;
    lo = 0;
    hi = count;   /* half-open interval */

    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;

        if (f_lseek(f, (FSIZE_t)mid * 4) != FR_OK)
        {
            return RT_FAIL;
        }
        if (f_read(f, rec, 4, &br) != FR_OK || br != 4)
        {
            return RT_FAIL;
        }

        /* record: unicode LE (rec[0..1]), gbk LE (rec[2..3]) */
        uint32_t u = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8);

        if (u == unicode)
        {
            /* GBK field is little-endian in the file: rec[2]=low, rec[3]=high.
             * Hand back the conventional (high, low) order. */
            gbk_out[0] = rec[3];
            gbk_out[1] = rec[2];
            return RT_OK;
        }
        else if (u < unicode)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }

    return RT_FAIL;   /* codepoint has no GBK mapping */
}

/* ---------------------------------------------------------------------------
 * SD card resident font descriptors (pTable == NULL -> read from file)
 *   Sizes = width/8 rounded up * height
 * -------------------------------------------------------------------------*/
pFONT CH_TEXT_Font12 = { NULL, 12, 12,  24, 0 };
pFONT CH_TEXT_Font16 = { NULL, 16, 16,  32, 0 };
pFONT CH_TEXT_Font24 = { NULL, 24, 24,  72, 0 };
pFONT CH_TEXT_Font32 = { NULL, 32, 32, 128, 0 };
