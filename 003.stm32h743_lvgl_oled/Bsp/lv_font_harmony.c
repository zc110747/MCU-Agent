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
#include "log.h"
#include "lv_font_harmony.h"
#include "lv_font_gbk.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

#if defined(LV_USE_TINY_TTF) && LV_USE_TINY_TTF && \
    defined(LV_TINY_TTF_FILE_SUPPORT) && LV_TINY_TTF_FILE_SUPPORT
    #define HARMONY_TTF_AVAILABLE   1
    #include "extra/libs/tiny_ttf/lv_tiny_ttf.h"
#else
    #define HARMONY_TTF_AVAILABLE   0
#endif

/*---------------------------------------------------------------------------*/
/* Configuration                                                              */
/*---------------------------------------------------------------------------*/

/* Keep in sync with the LV_FONT_DECLARE list in lv_font_gbk.h */
#define HARMONY_SIZES       4u

/* Descriptor cache depth per size.  The UI shows roughly 60 distinct
 * characters per size, so 64 keeps a steady state at zero card traffic. */
#define HARMONY_DSC_SLOTS   64u

#define HARMONY_NAME_MAX    64u
#define HARMONY_PATH_MAX    (sizeof(HARMONY_FONT_DIR) + HARMONY_NAME_MAX)

static const uint16_t s_sizes[HARMONY_SIZES] = { 12u, 16u, 24u, 32u };

/* Bitmap LRU budget per size, in bytes, drawn from the LVGL heap.  A glyph
 * costs box_w * box_h, so this is ~28 / 32 / 21 / 16 glyphs respectively. */
static const size_t   s_bmp_cache[HARMONY_SIZES] = { 4096u, 8192u, 12288u, 16384u };

/*---------------------------------------------------------------------------*/
/* Descriptor cache                                                           */
/*---------------------------------------------------------------------------*/

typedef struct
{
    uint32_t             letter;
    uint16_t             stamp;     /* value of the owning font's clock */
    uint8_t              used;
    lv_font_glyph_dsc_t  dsc;
} dsc_slot_t;

typedef struct
{
    lv_font_t   *ttf;               /* the tiny_ttf font we wrap */
    dsc_slot_t  *slots;
    uint16_t     n;
    uint16_t     clock;
} harmony_dsc_t;

static dsc_slot_t     s_slots[HARMONY_SIZES][HARMONY_DSC_SLOTS];
static harmony_dsc_t  s_fdsc[HARMONY_SIZES];
static lv_font_t      s_font[HARMONY_SIZES];
static uint8_t        s_valid[HARMONY_SIZES];

static uint8_t        s_ready;
static char           s_file[HARMONY_NAME_MAX];
static char           s_path[HARMONY_PATH_MAX];
static uint32_t       s_file_bytes;

static uint32_t       s_dsc_hits;
static uint32_t       s_dsc_misses;

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
    static const char *const pref[] = { "REGULAR", "MEDIUM", "BOLD", "LIGHT", "THIN", "BLACK" };

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

    return 2;   /* a font, but the weight is not in the table */
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
            continue;               /* not a font file, not interesting */
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

    strncpy(s_file, best, HARMONY_NAME_MAX - 1u);
    s_file[HARMONY_NAME_MAX - 1u] = '\0';
    s_file_bytes = best_bytes;

    {
        int n = snprintf(s_path, sizeof(s_path), "%s/%s", HARMONY_FONT_DIR, s_file);
        if ((n < 0) || ((size_t)n >= sizeof(s_path)))
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
        case 12u:  return &lv_font_gbk_12;
        case 24u:  return &lv_font_gbk_24;
        case 32u:  return &lv_font_gbk_32;
        default:   return &lv_font_gbk_16;
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
            h->slots[i].stamp = h->clock;
            *dsc_out = h->slots[i].dsc;
            dsc_out->resolved_font = font;
            s_dsc_hits++;
            return true;
        }
    }

    s_dsc_misses++;

    if (!h->ttf->get_glyph_dsc(h->ttf, dsc_out, letter, 0u))
    {
        return false;                       /* let LVGL try ->fallback */
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

    s_ready      = 0u;
    s_file[0]    = '\0';
    s_file_bytes = 0u;

    memset(s_valid, 0, sizeof(s_valid));
    memset(s_slots, 0, sizeof(s_slots));

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

        PRINT_LOG("[TTF ] %s (%lu B)\r\n", s_file, (unsigned long)s_file_bytes);

        for (i = 0u; i < HARMONY_SIZES; i++)
        {
            lv_font_t *ttf;
            uint32_t   t0;

            t0  = (uint32_t)HAL_GetTick();
            ttf = lv_tiny_ttf_create_file_ex(s_path, (lv_coord_t)s_sizes[i],
                                             s_bmp_cache[i]);

            if (ttf == NULL)
            {
                PRINT_LOG("[TTF ]   %2u px: create FAILED\r\n", (unsigned)s_sizes[i]);
                continue;
            }

            s_valid[i] = 1u;
            ok++;

            s_fdsc[i].ttf   = ttf;
            s_fdsc[i].slots = &s_slots[i][0];
            s_fdsc[i].n     = (uint16_t)HARMONY_DSC_SLOTS;
            s_fdsc[i].clock = 0u;

            memset(&s_font[i], 0, sizeof(s_font[i]));
            s_font[i].get_glyph_dsc       = harmony_get_glyph_dsc;
            s_font[i].get_glyph_bitmap    = harmony_get_glyph_bitmap;
            s_font[i].line_height         = ttf->line_height;
            s_font[i].base_line           = ttf->base_line;
            s_font[i].subpx               = ttf->subpx;
            s_font[i].underline_position  = ttf->underline_position;
            s_font[i].underline_thickness = ttf->underline_thickness;
            s_font[i].dsc                 = &s_fdsc[i];
            s_font[i].fallback            = gbk_fallback(s_sizes[i]);

            PRINT_LOG("[TTF ]   %2u px: line %d, base %d (%lu ms)\r\n",
                   (unsigned)s_sizes[i],
                   (int)s_font[i].line_height, (int)s_font[i].base_line,
                   (unsigned long)((uint32_t)HAL_GetTick() - t0));
        }

        if (ok == 0u)
        {
            PRINT_LOG("[TTF ] no usable font size - falling back to GBK\r\n");
            return RT_FAIL;
        }

        s_ready = 1u;
        return RT_OK;
    }
#endif
}

uint8_t lv_font_harmony_ready(void)
{
    return s_ready;
}

const lv_font_t *lv_font_harmony_get(uint16_t size)
{
    uint32_t i;

    if (s_ready == 0u)
    {
        return NULL;
    }

    for (i = 0u; i < HARMONY_SIZES; i++)
    {
        if (s_sizes[i] == size)
        {
            return (s_valid[i] != 0u) ? &s_font[i] : NULL;
        }
    }
    return NULL;
}

const char *lv_font_harmony_file(void)
{
    return s_file;
}

void lv_font_harmony_stats(uint32_t *hits, uint32_t *misses)
{
    if (hits != NULL)
    {
        *hits = s_dsc_hits;
    }
    if (misses != NULL)
    {
        *misses = s_dsc_misses;
    }
}

void lv_font_harmony_reset_cache(void)
{
    uint32_t i;

    memset(s_slots, 0, sizeof(s_slots));
    for (i = 0u; i < HARMONY_SIZES; i++)
    {
        s_fdsc[i].clock = 0u;
    }

    s_dsc_hits   = 0u;
    s_dsc_misses = 0u;
}
