/**
 * @file sd_font.cpp
 * @brief A GBK bitmap face read off the card and served to LVGL on demand.
 *
 * THE FILE FORMAT
 * ---------------
 * The card ships HZK-layout faces.  For a 16 px face the arithmetic is exact
 * and that is the whole of the format check:
 *
 *     GBK16.FON = 766080 bytes
 *                / 23940 glyphs = 32 bytes each = 16 rows x 2 bytes
 *                23940 = 126 lead bytes (0x81..0xFE)
 *                      x 190 trail bytes (0x40..0xFE, minus 0x7F)
 *
 * so a file of exactly 766080 bytes is a 16x16 GBK face laid out row by row,
 * lead-major, and anything else at that path is refused rather than read as
 * garbage.  Each row is MSB-first, one bit per pixel, no padding.
 *
 * FROM CODEPOINTS BACK TO GLYPHS
 * ------------------------------
 * LVGL hands a font a Unicode codepoint, but the file is indexed by the GBK
 * pair.  The GBK -> Unicode direction already exists (gbk_table.h, generated
 * from the host's cp936).  Rather than search it per glyph, the reverse table
 * is built once at install time: a flat 64 Ki array of glyph indices, 128 KB,
 * which turns the lookup on the render path into one load.  Code page 936 is
 * BMP-only, so the table being uint16-wide is not a limitation here.
 */
#include "sd_font.h"

#include <string.h>

#include "gbk_table.h"
#include "storage_service.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "sd_font";

namespace ui {
namespace {

/* ---- the 16 px face ---------------------------------------------------- */

constexpr int    kCell        = 16;
constexpr int    kBytesPerRow = kCell / 8;
constexpr size_t kGlyphBytes  = kCell * kBytesPerRow;   /* 32  */
constexpr size_t kGlyphCount  = 23940;
constexpr size_t kFileBytes   = kGlyphCount * kGlyphBytes;

static_assert(kGlyphCount * kGlyphBytes == 766080, "the size the card's GBK16.FON must have");

/* 0x7F is not a trail byte, so a lead's 191-byte trail range carries 190
 * glyphs.  That -1 is what makes the file 23940 glyphs rather than 24066, and
 * getting it wrong would shift every glyph after the first 0x7F by one. */
constexpr size_t kTrailsPerLead = 190;
constexpr uint8_t kNoTrail      = 0x7F;

/* The vertical placement, in LVGL's terms.  LVGL puts a glyph's box at
 *
 *     line_top + (line_height - base_line) - box_h - ofs_y
 *
 * so with the metrics copied from the embedded face (line_height 20,
 * base_line 5 -> baseline 15 rows down) an ofs_y of -2 puts this face's
 * 16-row cell at rows 1..16, i.e. its bottom one row below the baseline.
 * That is where the embedded face puts a full-height CJK glyph too, and its
 * 281 full-cell glyphs carry exactly (box_w 16, box_h 16, ofs_x 0, ofs_y -2).
 *
 * WHAT THE SWAP DOES AND DOES NOT MOVE
 * ------------------------------------
 * Horizontal layout is affected for exactly two characters.  Every CJK glyph in
 * the embedded face advances 256/16 = 16 px with ofs_x 0, which is the adv_w
 * and ofs_x set in face_glyph_dsc(), so line breaking and the reader's
 * pagination stay put - except for U+3001 '、' (141/16 = 8.81 px) and U+FF08
 * '（' (258/16 = 16.125 px), which were rasterised into that subset with
 * fractional advances.  This face gives them a real 16 px cell, so '、' gets
 * 7.2 px wider and can push the last character of a line onto the next one.
 * That is a correction rather than a regression (an ideographic comma is a
 * full-width character), but it is a visible difference.  tools/verify_sd_font.py
 * pins the pair so any further drift is loud.
 *
 * Line height is identical because line_height and base_line are copied.
 *
 * Vertical ink can differ by up to 1 px: the embedded face is a rendered
 * outline font and crops each glyph tightly (box_h runs 14..17, ofs_y -1 or
 * -2 across its 1434 glyphs), while a 16x16 bitmap face has one cell size for
 * every glyph.  Ink in a dense cell therefore reaches one row lower than the
 * embedded face would have drawn it - a sub-pixel-scale difference, not a
 * reflow. */
constexpr int kOffsetY = -2;

/* Not a glyph index.  0 is a real glyph (lead 0x81, trail 0x40) so it cannot
 * double as "absent". */
constexpr uint16_t kNoGlyph = 0xFFFF;

/* Long-file-name FAT is case-insensitive, so one of these is normally enough;
 * both are listed so the card can be prepared on a case-sensitive host too. */
const char *const kPaths[] = {
    "/sd/fonts/GBK16.FON",
    "/sd/fonts/gbk16.fon",
};

/** @brief Glyph index for a GBK pair, in the file's lead-major layout. */
inline size_t hzk_index(uint8_t lead, uint8_t trail)
{
    return (size_t)(lead - GBK_LEAD_MIN) * kTrailsPerLead
         + (size_t)(trail - GBK_TRAIL_MIN)
         - ((trail > kNoTrail) ? 1u : 0u);
}

struct Face {
    lv_font_t       font;
    const uint8_t  *glyphs;   /* PSRAM: kFileBytes bytes, the file verbatim   */
    uint16_t       *codepoint;/* PSRAM: kCodepoints entries, kNoGlyph = none  */
    bool            ready;
};

Face s_face;
bool s_tried;

constexpr size_t kCodepoints = 0x10000;   /* the table is uint16 -> BMP only */

/* ---- the LVGL callbacks ------------------------------------------------ */

/**
 * @brief Codepoint -> glyph.
 *
 * Returning false is meaningful here: lv_font_get_glyph_dsc() then walks
 * lv_font_t::fallback, which is the embedded CJK face.  That is how ASCII,
 * the FontAwesome block LVGL's LV_SYMBOL_* lives in (this face starts at GBK
 * 0x8140 and has no PUA cells at all), and any GBK slot cp936 leaves
 * undefined keep working - the card's face covers the double-byte area and
 * nothing else.
 */
bool face_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
                    uint32_t letter, uint32_t letter_next)
{
    (void)font;
    (void)letter_next;   /* no kerning: every glyph here is a full-width cell */

    if (letter >= kCodepoints) {
        return false;
    }
    const uint16_t gid = s_face.codepoint[letter];
    if (gid == kNoGlyph) {
        return false;
    }

    /* The glyph cache keys on this, and 0 means "no glyph" to it, so the
     * index is stored one-based. */
    dsc->gid.index = (uint32_t)gid + 1;
    dsc->adv_w     = kCell;
    dsc->box_w     = kCell;
    dsc->box_h     = kCell;
    dsc->ofs_x     = 0;
    dsc->ofs_y     = kOffsetY;
    /* 0 means "no padding at the end of a line"; the consumer takes the row
     * pitch from the draw buffer it handed us instead. */
    dsc->stride    = 0;
    dsc->format    = LV_FONT_GLYPH_FORMAT_A8;
    return true;
}

/**
 * @brief Monochrome row-major source -> the A8 buffer LVGL supplies.
 *
 * Either A1 or A8 would do, but LVGL asks for A8 when req_raw_bitmap is 0 -
 * and the draw code reads the result back through an lv_draw_buf_t, taking the
 * row pitch from its header.  Writing at header.stride rather than at box_w is
 * what keeps that consistent if the buffer is ever allocated with padding.
 */
const void *face_glyph_bitmap(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf)
{
    if (draw_buf == nullptr || draw_buf->data == nullptr || dsc->gid.index == 0) {
        return nullptr;
    }
    const size_t gid = (size_t)dsc->gid.index - 1;
    if (gid >= kGlyphCount) {
        return nullptr;
    }

    const uint8_t *const glyph = s_face.glyphs + gid * kGlyphBytes;
    uint8_t *const  out        = draw_buf->data;
    const uint32_t  pitch      = draw_buf->header.stride;

    for (int y = 0; y < kCell; ++y) {
        uint8_t       *row_out = out + (size_t)y * pitch;
        const uint8_t *row_in  = glyph + (size_t)y * kBytesPerRow;

        /* The draw code extends the mask across the whole pitch, so the
         * padding must be a real 0 rather than whatever was in the buffer. */
        memset(row_out, 0, pitch);
        for (int x = 0; x < kCell; ++x) {
            row_out[x] = (row_in[x >> 3] & (0x80u >> (x & 7))) ? 0xFF : 0x00;
        }
    }

    lv_draw_buf_flush_cache(draw_buf, nullptr);
    return draw_buf;
}

/* ---- install ----------------------------------------------------------- */

void build_codepoint_index(uint16_t *index)
{
    for (size_t i = 0; i < kCodepoints; ++i) {
        index[i] = kNoGlyph;
    }

    for (uint32_t lead = GBK_LEAD_MIN; lead <= GBK_LEAD_MAX; ++lead) {
        for (uint32_t trail = GBK_TRAIL_MIN; trail <= GBK_TRAIL_MAX; ++trail) {
            if (trail == kNoTrail) {
                continue;
            }
            const uint16_t uni = gbk_unicode_table[(lead - GBK_LEAD_MIN) * GBK_TRAIL_COUNT +
                                                  (trail - GBK_TRAIL_MIN)];
            if (uni != 0) {
                index[uni] = (uint16_t)hzk_index((uint8_t)lead, (uint8_t)trail);
            }
        }
    }
}

/** @brief The first candidate path whose size is exactly the face we know. */
const char *find_face()
{
    for (const char *path : kPaths) {
        const uint32_t size = services::storage_file_size(path);
        if (size == (uint32_t)kFileBytes) {
            return path;
        }
        if (size != 0) {
            ESP_LOGW(TAG, "%s is %u bytes, not the %u of a 16x16 GBK face - ignored",
                     path, (unsigned)size, (unsigned)kFileBytes);
        }
    }
    return nullptr;
}

}  // namespace

esp_err_t sd_font_install()
{
    if (s_face.ready) {
        return ESP_OK;
    }
    if (s_tried) {
        return ESP_ERR_NOT_FOUND;
    }
    s_tried = true;

    if (!services::storage_ready()) {
        ESP_LOGI(TAG, "no card, keeping the embedded CJK face");
        return ESP_ERR_NOT_FOUND;
    }

    const char *path = find_face();
    if (path == nullptr) {
        ESP_LOGI(TAG, "no /sd/fonts/GBK16.FON on the card, keeping the embedded CJK face");
        return ESP_ERR_NOT_FOUND;
    }

    /* PSRAM rather than internal: this is 894 KB of read-mostly data, and the
     * internal heap is what the RGB bounce buffers and the WiFi stack need. */
    uint8_t  *glyphs = (uint8_t *)heap_caps_malloc(kFileBytes, MALLOC_CAP_SPIRAM);
    uint16_t *index  = (uint16_t *)heap_caps_malloc(kCodepoints * sizeof(uint16_t),
                                                   MALLOC_CAP_SPIRAM);
    if (glyphs == nullptr || index == nullptr) {
        ESP_LOGE(TAG, "cannot spare %u KB of PSRAM, keeping the embedded CJK face",
                 (unsigned)((kFileBytes + kCodepoints * sizeof(uint16_t)) / 1024));
        heap_caps_free(glyphs);
        heap_caps_free(index);
        return ESP_ERR_NO_MEM;
    }

    size_t got = 0;
    const esp_err_t err = services::storage_read(path, glyphs, kFileBytes, &got);
    if (err != ESP_OK || got != kFileBytes) {
        ESP_LOGE(TAG, "reading %s: %s (%u of %u bytes)", path, esp_err_to_name(err),
                 (unsigned)got, (unsigned)kFileBytes);
        heap_caps_free(glyphs);
        heap_caps_free(index);
        return (err != ESP_OK) ? err : ESP_ERR_INVALID_SIZE;
    }

    build_codepoint_index(index);

    s_face.glyphs    = glyphs;
    s_face.codepoint = index;
    s_face.font.get_glyph_dsc    = face_glyph_dsc;
    s_face.font.get_glyph_bitmap = face_glyph_bitmap;
    s_face.font.release_glyph    = nullptr;   /* nothing is allocated per glyph */
    /* Metrics copied from the embedded face on purpose - they decide where
     * every glyph lands, including the ones that come from the fallback. */
    s_face.font.line_height     = 20;
    s_face.font.base_line       = 5;
    s_face.font.subpx           = LV_FONT_SUBPX_NONE;
    s_face.font.kerning         = LV_FONT_KERNING_NONE;
    s_face.font.static_bitmap   = 0;
    s_face.font.underline_position  = -2;
    s_face.font.underline_thickness = 1;
    s_face.font.dsc             = nullptr;   /* state lives in s_face */
    s_face.font.fallback        = &lv_font_source_han_sans_sc_16_cjk;
    s_face.font.user_data       = nullptr;
    s_face.ready                = true;

    /* Count what actually resolved rather than what the file could hold: the
     * difference is the part of cp936 the host's code page does not define. */
    size_t mapped = 0;
    for (size_t i = 0; i < kCodepoints; ++i) {
        if (index[i] != kNoGlyph) {
            ++mapped;
        }
    }
    ESP_LOGI(TAG, "installed %s: %u glyphs, %u codepoints mapped, %u KB PSRAM",
             path, (unsigned)kGlyphCount, (unsigned)mapped,
             (unsigned)((kFileBytes + kCodepoints * sizeof(uint16_t)) / 1024));
    return ESP_OK;
}

const lv_font_t *sd_font_cjk()
{
    return s_face.ready ? &s_face.font : nullptr;
}

}  // namespace ui
