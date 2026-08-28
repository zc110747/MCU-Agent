/**
  ******************************************************************************
  * @file    emwin_font_gbk.c
  * @brief   Custom emWin font: UTF-8 -> GBK -> SD FON, with an LRU glyph cache.
  *
  *  Both the compiled ASCII tables (ASCII_Fontxx) and the SD-backed Chinese
  *  glyphs (lcd_driver_get_hzmat) store their bitmaps in the SAME layout that
  *  emWin's monochrome drawing expects: LSB first, row scan.  A single draw
  *  routine (draw_lsb_rows) therefore renders either kind of glyph, which
  *  keeps the font model simple and avoids any bitmap-repacking step.
  ******************************************************************************
  */
#include "emwin_font_gbk.h"

/* ---- on-demand Chinese glyph cache -------------------------------------- */
#define GBK_CACHE_SLOTS 24
#define GBK_GLYPH_MAX   128   /* 32x32 glyph -> 128 bytes */

typedef struct
{
    uint8_t  valid;
    uint8_t  height;
    uint16_t gbk;
    uint8_t  bits[GBK_GLYPH_MAX];
} gbk_cache_t;

static gbk_cache_t g_cache[GBK_CACHE_SLOTS];
static int         g_cache_rr  = 0;
static uint32_t    g_cache_hit  = 0;
static uint32_t    g_cache_read = 0;

/* ---- font selection by pixel height ------------------------------------- */
static const pFONT * ch_font_for_height(uint8_t h)
{
    switch (h)
    {
        case 12: return &CH_TEXT_Font12;
        case 16: return &CH_TEXT_Font16;
        case 24: return &CH_TEXT_Font24;
        case 32: return &CH_TEXT_Font32;
        default: return &CH_TEXT_Font16;
    }
}

static const pFONT * ascii_font_for_height(uint8_t h)
{
    switch (h)
    {
        case 12: return &ASCII_Font12;
        case 16: return &ASCII_Font16;
        case 20: return &ASCII_Font20;
        case 24: return &ASCII_Font24;
        case 32: return &ASCII_Font32;
        default: return &ASCII_Font16;
    }
}

/* ---- draw a LSB-first, row-scan bitmap at (x0,y0) ----------------------- */
static void draw_lsb_rows(int x0, int y0, int w, int h, const uint8_t * bits)
{
    int bpr = (w + 7) >> 3;
    for (int row = 0; row < h; row++)
    {
        const uint8_t * line = bits + (size_t)row * (size_t)bpr;
        for (int col = 0; col < w; col++)
        {
            if (line[col >> 3] & (uint8_t)(1u << (col & 7)))
            {
                GUI_DrawPixel(x0 + col, y0 + row);
            }
        }
    }
}

/* ---- Chinese glyph cache lookup (returns LSB row-scan bits) ------------- */
static const uint8_t * gbk_cache_lookup(uint8_t height, uint16_t gbk, const pFONT * font)
{
    int empty = -1;
    for (int i = 0; i < GBK_CACHE_SLOTS; i++)
    {
        if (g_cache[i].valid && g_cache[i].height == height && g_cache[i].gbk == gbk)
        {
            g_cache_hit++;
            return g_cache[i].bits;
        }
        if (!g_cache[i].valid && empty < 0)
        {
            empty = i;
        }
    }

    /* Miss: read from SD (LSB row-scan), store, evict round-robin if full. */
    uint8_t code[2] = { (uint8_t)(gbk >> 8), (uint8_t)(gbk & 0xFF) };
    int slot = (empty >= 0) ? empty : (g_cache_rr = (g_cache_rr + 1) % GBK_CACHE_SLOTS);

    lcd_driver_get_hzmat(code, g_cache[slot].bits, (pFONT *)font);
    g_cache[slot].valid  = 1;
    g_cache[slot].height = height;
    g_cache[slot].gbk    = gbk;
    g_cache_read++;
    return g_cache[slot].bits;
}

/* ---- emWin font methods ------------------------------------------------- */
static void emwin_gbk_disp_char(U16 c)
{
    const GUI_FONT * pFont = GUI_GetFont();
    uint8_t h = (uint8_t)(pFont ? pFont->YSize : 16);
    int x0 = GUI_GetDispPosX();
    int y0 = GUI_GetDispPosY();

    if (c < 0x80U)
    {
        if (c < 0x20U) c = 0x20U;                 /* control chars -> space */
        const pFONT * af = ascii_font_for_height(h);
        if (af->pTable == NULL) return;
        const uint8_t * bits = af->pTable + (size_t)(c - 0x20U) * af->Sizes;
        draw_lsb_rows(x0, y0, (int)af->Width, (int)af->Height, bits);
    }
    else
    {
        uint16_t gbk = lv_gbk_from_unicode((uint32_t)c);
        if (gbk == 0U) return;                   /* unmapped -> blank */
        const pFONT * cf = ch_font_for_height(h);
        const uint8_t * bits = gbk_cache_lookup(h, gbk, cf);
        if (bits != NULL)
        {
            draw_lsb_rows(x0, y0, (int)cf->Width, (int)cf->Height, bits);
        }
    }
}

static int emwin_gbk_get_char_distx(U16P c, int * pSizeX)
{
    const GUI_FONT * pFont = GUI_GetFont();
    uint8_t h = (uint8_t)(pFont ? pFont->YSize : 16);
    int w;
    int dist;
    if (c < 0x80U)
    {
        const pFONT * af = ascii_font_for_height(h);
        w = (int)af->Width;
        dist = w + 1;                            /* 1px gap between ASCII */
    }
    else
    {
        const pFONT * cf = ch_font_for_height(h);
        w = (int)cf->Width;
        dist = w;                                /* Chinese glyphs touch */
    }
    if (pSizeX != NULL) *pSizeX = w;
    return dist;
}

static int emwin_gbk_get_char_info(U16P c, GUI_CHARINFO_EXT * pInfo)
{
    const GUI_FONT * pFont = GUI_GetFont();
    uint8_t h = (uint8_t)(pFont ? pFont->YSize : 16);
    int w;
    int ht;
    if (c < 0x80U)
    {
        const pFONT * af = ascii_font_for_height(h);
        w = (int)af->Width;
        ht = (int)af->Height;
    }
    else
    {
        const pFONT * cf = ch_font_for_height(h);
        w = (int)cf->Width;
        ht = (int)cf->Height;
    }
    pInfo->XSize  = (U8)w;
    pInfo->YSize  = (U8)ht;
    pInfo->XPos   = 0;
    pInfo->YPos   = 0;
    pInfo->XDist  = (U8)(c < 0x80U ? (w + 1) : w);
    pInfo->pData  = NULL;
    return 0;
}

static void emwin_gbk_get_font_info(const GUI_FONT * pFont, GUI_FONTINFO * pfi)
{
    pfi->Flags    = GUI_FONTINFO_FLAG_PROP;
    pfi->Baseline = (U8)(pFont ? pFont->Baseline : 13);
    pfi->LHeight  = (U8)(pFont ? pFont->LHeight  : 16);
    pfi->CHeight  = (U8)(pFont ? pFont->CHeight  : 16);
}

static char emwin_gbk_is_in_font(const GUI_FONT * pFont, U16 c)
{
    (void)pFont;
    return (c >= 0x20U) ? 1 : 0;
}

/* ---- exported cache statistics (for the "缓存" line) -------------------- */
void emwin_font_get_cache_stats(uint32_t * hit, uint32_t * read)
{
    if (hit  != NULL) *hit  = g_cache_hit;
    if (read != NULL) *read = g_cache_read;
}

/* ---- the four font objects --------------------------------------------- */
const GUI_FONT EMWIN_FONT_GBK12 =
{
  .pfDispChar     = emwin_gbk_disp_char,
  .pfGetCharDistX = emwin_gbk_get_char_distx,
  .pfGetFontInfo  = emwin_gbk_get_font_info,
  .pfIsInFont     = emwin_gbk_is_in_font,
  .pfGetCharInfo  = emwin_gbk_get_char_info,
  .pafEncode      = &GUI_ENC_APIList_EXT,
  .YSize = 12, .YDist = 12, .XMag = 1, .YMag = 1,
  .Baseline = 10, .LHeight = 12, .CHeight = 12,
};

const GUI_FONT EMWIN_FONT_GBK16 =
{
  .pfDispChar     = emwin_gbk_disp_char,
  .pfGetCharDistX = emwin_gbk_get_char_distx,
  .pfGetFontInfo  = emwin_gbk_get_font_info,
  .pfIsInFont     = emwin_gbk_is_in_font,
  .pfGetCharInfo  = emwin_gbk_get_char_info,
  .pafEncode      = &GUI_ENC_APIList_EXT,
  .YSize = 16, .YDist = 16, .XMag = 1, .YMag = 1,
  .Baseline = 13, .LHeight = 16, .CHeight = 16,
};

const GUI_FONT EMWIN_FONT_GBK24 =
{
  .pfDispChar     = emwin_gbk_disp_char,
  .pfGetCharDistX = emwin_gbk_get_char_distx,
  .pfGetFontInfo  = emwin_gbk_get_font_info,
  .pfIsInFont     = emwin_gbk_is_in_font,
  .pfGetCharInfo  = emwin_gbk_get_char_info,
  .pafEncode      = &GUI_ENC_APIList_EXT,
  .YSize = 24, .YDist = 24, .XMag = 1, .YMag = 1,
  .Baseline = 19, .LHeight = 24, .CHeight = 24,
};

const GUI_FONT EMWIN_FONT_GBK32 =
{
  .pfDispChar     = emwin_gbk_disp_char,
  .pfGetCharDistX = emwin_gbk_get_char_distx,
  .pfGetFontInfo  = emwin_gbk_get_font_info,
  .pfIsInFont     = emwin_gbk_is_in_font,
  .pfGetCharInfo  = emwin_gbk_get_char_info,
  .pafEncode      = &GUI_ENC_APIList_EXT,
  .YSize = 32, .YDist = 32, .XMag = 1, .YMag = 1,
  .Baseline = 26, .LHeight = 32, .CHeight = 32,
};
