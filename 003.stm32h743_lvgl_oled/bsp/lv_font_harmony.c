/**
 ******************************************************************************
 * @file    lv_font_harmony.c
 * @brief   LVGL fonts rendered from HarmonyOS Sans TC .ttf files on the SD card.
 * @see     lv_font_harmony.h
 *
 *  Rendering pipeline
 *  ------------------
 *      LVGL  ->  harmony_get_glyph_dsc()      descriptor cache (RAM)
 *                     | miss
 *                     v
 *              tiny_ttf / stb_truetype       random access into the .ttf
 *                     |
 *                     v
 *              lv_port_fs block cache        RAM
 *                     |
 *                     v
 *              FatFs / SDMMC                 SD card
 *
 *  The descriptor cache is what makes this usable: LVGL re-measures every
 *  character on every layout and every draw pass, and stb_truetype in stream
 *  mode turns one measurement into a few dozen seek+1-byte-read pairs.
 ******************************************************************************
 */
#include "bsp_log.h"
#include "lv_font_harmony.h"
#include "lv_font_gbk.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

#if defined(LV_USE_TINY_TTF) && LV_USE_TINY_TTF && \
    defined(LV_TINY_TTF_FILE_SUPPORT) && LV_TINY_TTF_FILE_SUPPORT
#define HARMONY_TTF_AVAILABLE 1
#include "extra/libs/tiny_ttf/lv_tiny_ttf.h"
#else
#define HARMONY_TTF_AVAILABLE 0
#endif

/*---------------------------------------------------------------------------*/
/* Configuration                                                              */
/*---------------------------------------------------------------------------*/

/* Keep in sync with the LV_FONT_DECLARE list in lv_font_gbk.h */
#define HARMONY_SIZES 4u

/* Descriptor cache depth per size.  The UI shows roughly 60 distinct
 * characters per size, so 64 keeps a steady state at zero card traffic. */
#define HARMONY_DSC_SLOTS 64u

#define HARMONY_NAME_MAX 64u
#define HARMONY_PATH_MAX (sizeof(HARMONY_FONT_DIR) + HARMONY_NAME_MAX)

static const uint16_t s_sizes[HARMONY_SIZES] = {12u, 16u, 24u, 32u};

/* Bitmap LRU budget per size, in bytes, drawn from the LVGL heap.  A glyph
 * costs box_w * box_h, so this is ~28 / 32 / 21 / 16 glyphs respectively. */
static const size_t s_bmp_cache[HARMONY_SIZES] = {4096u, 8192u, 12288u, 16384u};

/*---------------------------------------------------------------------------*/
/* Descriptor cache                                                           */
/*---------------------------------------------------------------------------*/

typedef struct
{
    uint32_t            letter;
    uint16_t            stamp; /* value of the owning font's clock */
    uint8_t             used;
    lv_font_glyph_dsc_t dsc;
} dsc_slot_t;

typedef struct
{
    lv_font_t  *ttf; /* the tiny_ttf font we wrap */
    dsc_slot_t *slots;
    uint16_t    n;
    uint16_t    clock;
} harmony_dsc_t;

/* HarmonyOS 引擎状态：字体对象、描述符缓存与当前文件 */
typedef struct
{
    dsc_slot_t    slots[HARMONY_SIZES][HARMONY_DSC_SLOTS];
    harmony_dsc_t fdsc[HARMONY_SIZES];
    lv_font_t     font[HARMONY_SIZES];
    uint8_t       valid[HARMONY_SIZES];
    uint8_t       ready;
    char          file[HARMONY_NAME_MAX];
    char          path[HARMONY_PATH_MAX];
    uint32_t      file_bytes;
} harmony_state_t;

static harmony_state_t g_harmony = {0};

/* 描述符缓存命中统计 */
typedef struct
{
    uint32_t dsc_hits;
    uint32_t dsc_misses;
} harmony_stats_t;

static harmony_stats_t g_stats = {0};

/*---------------------------------------------------------------------------*/
/* Weight selection                                                           */
/*---------------------------------------------------------------------------*/

static void upper_copy(char *dst, size_t dst_size, const char *src)
{
    size_t i;

    for (i = 0u; (src[i] != '\0') && (i < (dst_size - 1u)); i++)
    {
        char c = src[i];
        if ((c >= 'a') && (c <= 'z'))
        {
            c = (char)(c - 'a' + 'A');
        }
        dst[i] = c;
    }
    dst[i] = '\0';
}

static int ends_with(const char *name, const char *ext)
{
    size_t nl = strlen(name);
    size_t el = strlen(ext);

    if (nl < el)
    {
        return 0;
    }

    {
        char tail[16];
        upper_copy(tail, sizeof(tail), name + (nl - el));
        return (strcmp(tail, ext) == 0) ? 1 : 0;
    }
}

/**
 * @brief  Rank a file name by how desirable it is as the UI font.
 * @retval higher is better, 0 means "not a font file".
 */
static int score_name(const char *name)
{
    static const char *const pref[] = {"REGULAR", "MEDIUM", "BOLD", "LIGHT", "THIN", "BLACK"};

    char     up[HARMONY_NAME_MAX];
    uint32_t i;
    int      count = (int)(sizeof(pref) / sizeof(pref[0]));

    if (!ends_with(name, ".TTF") && !ends_with(name, ".TTC"))
    {
        return 0;
    }

    upper_copy(up, sizeof(up), name);

    /* Italic is a fallback of last resort - never pick it over upright. */
    if (strstr(up, "ITALIC") != NULL)
    {
        return 1;
    }

    for (i = 0u; i < (uint32_t)count; i++)
    {
        if (strstr(up, pref[i]) != NULL)
        {
            return 10 - (int)i;
        }
    }

    return 2; /* a font, but the weight is not in the table */
}

/*---------------------------------------------------------------------------*/
/* Directory scan                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief  Pick the best *.ttf in HARMONY_FONT_DIR, listing everything found.
 */
static GlobalType_t harmony_scan(void)
{
    DIR      dir;
    FILINFO  fno;
    FRESULT  res;
    char     best[HARMONY_NAME_MAX];
    int      best_score = 0;
    uint32_t best_bytes = 0u;

    best[0] = '\0';

    res = f_opendir(&dir, HARMONY_FONT_DIR);
    if (res != FR_OK)
    {
        PRINT_LOG("[TTF ] %s: opendir failed (%d)\r\n", HARMONY_FONT_DIR, (int)res);
        return RT_FAIL;
    }

    PRINT_LOG("[TTF ] scanning %s\r\n", HARMONY_FONT_DIR);

    for (;;)
    {
        int sc;

        res = f_readdir(&dir, &fno);
        if ((res != FR_OK) || (fno.fname[0] == '\0'))
        {
            break;
        }
        if ((fno.fattrib & AM_DIR) != 0u)
        {
            continue;
        }

        sc = score_name(fno.fname);
        if (sc == 0)
        {
            continue; /* not a font file, not interesting */
        }

        PRINT_LOG("[TTF ]   %-40s %8lu B  score %d\r\n",
                  fno.fname, (unsigned long)fno.fsize, sc);

        if (sc > best_score)
        {
            best_score = sc;
            best_bytes = (uint32_t)fno.fsize;
            strncpy(best, fno.fname, HARMONY_NAME_MAX - 1u);
            best[HARMONY_NAME_MAX - 1u] = '\0';
        }
    }

    (void)f_closedir(&dir);

    if (best_score == 0)
    {
        PRINT_LOG("[TTF ] no .ttf / .ttc found\r\n");
        return RT_FAIL;
    }

    strncpy(g_harmony.file, best, HARMONY_NAME_MAX - 1u);
    g_harmony.file[HARMONY_NAME_MAX - 1u] = '\0';
    g_harmony.file_bytes                  = best_bytes;

    {
        int n = snprintf(g_harmony.path, sizeof(g_harmony.path), "%s/%s", HARMONY_FONT_DIR, g_harmony.file);
        if ((n < 0) || ((size_t)n >= sizeof(g_harmony.path)))
        {
            PRINT_LOG("[TTF ] path too long\r\n");
            return RT_FAIL;
        }
    }

    return RT_OK;
}

/*---------------------------------------------------------------------------*/
/* LVGL callbacks                                                             */
/*---------------------------------------------------------------------------*/

/**
 * @brief  GBK bitmap font used when the .ttf has no glyph for a code point.
 */
static const lv_font_t *gbk_fallback(uint16_t size)
{
    switch (size)
    {
    case 12u:
        return &lv_font_gbk_12;
    case 24u:
        return &lv_font_gbk_24;
    case 32u:
        return &lv_font_gbk_32;
    default:
        return &lv_font_gbk_16;
    }
}

static bool harmony_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                  uint32_t letter, uint32_t letter_next)
{
    harmony_dsc_t *h = (harmony_dsc_t *)font->dsc;
    uint16_t       i;
    uint16_t       victim = 0u;
    uint16_t       oldest = 0u;

    /* Kerning is intentionally ignored - see the note in lv_font_harmony.h. */
    LV_UNUSED(letter_next);

    h->clock++;

    for (i = 0u; i < h->n; i++)
    {
        if ((h->slots[i].used != 0u) && (h->slots[i].letter == letter))
        {
            h->slots[i].stamp      = h->clock;
            *dsc_out               = h->slots[i].dsc;
            dsc_out->resolved_font = font;
            g_stats.dsc_hits++;
            return true;
        }
    }

    g_stats.dsc_misses++;

    if (!h->ttf->get_glyph_dsc(h->ttf, dsc_out, letter, 0u))
    {
        return false; /* let LVGL try ->fallback */
    }

    if (dsc_out->is_placeholder)
    {
        dsc_out->resolved_font = font;
        return true;
    }

    /* Free slot if there is one, otherwise the least recently used one.
     * h->clock wraps, so rank by distance from "now" rather than raw value. */
    for (i = 0u; i < h->n; i++)
    {
        uint16_t dist;

        if (h->slots[i].used == 0u)
        {
            victim = i;
            break;
        }

        dist = (uint16_t)(h->clock - h->slots[i].stamp);
        if (dist >= oldest)
        {
            oldest = dist;
            victim = i;
        }
    }

    h->slots[victim].used   = 1u;
    h->slots[victim].letter = letter;
    h->slots[victim].stamp  = h->clock;
    h->slots[victim].dsc    = *dsc_out;

    dsc_out->resolved_font = font;
    return true;
}

static const uint8_t *harmony_get_glyph_bitmap(const lv_font_t *font, uint32_t letter)
{
    harmony_dsc_t *h = (harmony_dsc_t *)font->dsc;

    return h->ttf->get_glyph_bitmap(h->ttf, letter);
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                 */
/*---------------------------------------------------------------------------*/

GlobalType_t lv_font_harmony_init(void)
{
    uint32_t i;

    g_harmony.ready      = 0u;
    g_harmony.file[0]    = '\0';
    g_harmony.file_bytes = 0u;

    memset(g_harmony.valid, 0, sizeof(g_harmony.valid));
    memset(g_harmony.slots, 0, sizeof(g_harmony.slots));

#if HARMONY_TTF_AVAILABLE == 0
    PRINT_LOG("[TTF ] disabled: LV_USE_TINY_TTF / LV_TINY_TTF_FILE_SUPPORT is 0\r\n");
    return RT_FAIL;
#else
    {
        uint32_t ok = 0u;

        if (harmony_scan() != RT_OK)
        {
            return RT_FAIL;
        }

        PRINT_LOG("[TTF ] %s (%lu B)\r\n", g_harmony.file, (unsigned long)g_harmony.file_bytes);

        for (i = 0u; i < HARMONY_SIZES; i++)
        {
            lv_font_t *ttf;
            uint32_t   t0;

            t0  = (uint32_t)HAL_GetTick();
            ttf = lv_tiny_ttf_create_file_ex(g_harmony.path, (lv_coord_t)s_sizes[i],
                                             s_bmp_cache[i]);

            if (ttf == NULL)
            {
                PRINT_LOG("[TTF ]   %2u px: create FAILED\r\n", (unsigned)s_sizes[i]);
                continue;
            }

            g_harmony.valid[i] = 1u;
            ok++;

            g_harmony.fdsc[i].ttf   = ttf;
            g_harmony.fdsc[i].slots = &g_harmony.slots[i][0];
            g_harmony.fdsc[i].n     = (uint16_t)HARMONY_DSC_SLOTS;
            g_harmony.fdsc[i].clock = 0u;

            memset(&g_harmony.font[i], 0, sizeof(g_harmony.font[i]));
            g_harmony.font[i].get_glyph_dsc       = harmony_get_glyph_dsc;
            g_harmony.font[i].get_glyph_bitmap    = harmony_get_glyph_bitmap;
            g_harmony.font[i].line_height         = ttf->line_height;
            g_harmony.font[i].base_line           = ttf->base_line;
            g_harmony.font[i].subpx               = ttf->subpx;
            g_harmony.font[i].underline_position  = ttf->underline_position;
            g_harmony.font[i].underline_thickness = ttf->underline_thickness;
            g_harmony.font[i].dsc                 = &g_harmony.fdsc[i];
            g_harmony.font[i].fallback            = gbk_fallback(s_sizes[i]);

            PRINT_LOG("[TTF ]   %2u px: line %d, base %d (%lu ms)\r\n",
                      (unsigned)s_sizes[i],
                      (int)g_harmony.font[i].line_height, (int)g_harmony.font[i].base_line,
                      (unsigned long)((uint32_t)HAL_GetTick() - t0));
        }

        if (ok == 0u)
        {
            PRINT_LOG("[TTF ] no usable font size - falling back to GBK\r\n");
            return RT_FAIL;
        }

        g_harmony.ready = 1u;
        return RT_OK;
    }
#endif
}

void lv_font_harmony_stats(uint32_t *hits, uint32_t *misses)
{
    if (hits != NULL)
    {
        *hits = g_stats.dsc_hits;
    }
    if (misses != NULL)
    {
        *misses = g_stats.dsc_misses;
    }
}
