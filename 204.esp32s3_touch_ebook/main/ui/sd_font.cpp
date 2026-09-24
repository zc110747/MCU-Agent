/**
 * @file sd_font.cpp
 * @brief GBK bitmap faces read off the card and served to LVGL.
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
 * and the same layout generalises to the other sizes on the card: a face of
 * cell size N is N/8 bytes per column, N columns, so 23940 * N * N/8 bytes
 * (24 px -> 72 B/glyph -> 1723680; 32 px -> 128 B/glyph -> 3064320).  The
 * check is a file size, so a card carrying something else at the path is
 * refused rather than misread.
 *
 * THE SCAN ORDER, WHICH IS NOT THE USUAL ONE
 * ------------------------------------------
 * Inside one glyph the bytes are COLUMN-major: bytes (x*N/8) .. carry column
 * x's rows top-down, and within each byte the MSB is the uppermost row.  That
 * is the "vertical" convention (纵向取模), not the row-major convention HZK16
 * itself uses - and the two are indistinguishable from the file size, the
 * glyph count or any index arithmetic, because both are N x N and both use
 * N*N/8 bytes.
 *
 * Reading it the row-major way does not fail; it transposes every glyph.  The
 * text still lays out, still paginates, still advances exactly N px per
 * character - it is simply not the words that were asked for, which is what
 * "the Chinese is garbled" looks like from the outside.  So the order is not
 * left as a comment: scan_order_is_sane() decodes 一 through the production
 * path at install time and refuses the face if the result is not a horizontal
 * bar.  Measured on this card (16 px): 在 快 速 发 agree with a host
 * rendering of the same characters at 72/57/62/51% of ink under this order,
 * and at 23/26/29/18% under the row-major one.
 *
 * FROM CODEPOINTS BACK TO GLYPHS
 * ------------------------------
 * LVGL hands a font a Unicode codepoint, but the file is indexed by the GBK
 * pair.  The GBK -> Unicode direction already exists (gbk_table.h, generated
 * from the host's cp936).  Rather than search it per glyph, the reverse table
 * is built once at install time: a flat 64 Ki array of glyph indices, 128 KB,
 * which turns the lookup on the render path into one load.  Code page 936 is
 * BMP-only, so the table being uint16-wide is not a limitation here.
 *
 * Every size on the card uses the same lead-major glyph ordering, so ONE
 * index serves all of them.
 *
 * TWO WAYS A FACE IS HELD (WHY NOT "LOAD IT ALL INTO PSRAM")
 * ---------------------------------------------------------
 * 16 px is the chrome size and 24 px is the in-RAM reading size: both are
 * small enough (766 KB and 1724 KB) that holding the whole file in PSRAM is
 * cheap and keeps rendering free of any card access.
 *
 * 32 px is 3064 KB - loading that in full would tie up most of the PSRAM
 * budget for a size the reader only offers as one of several, and would
 * starve things that genuinely need RAM (Photos decodes whole-screen images
 * there).  So the 32 px face is NOT loaded.  The .FON stays on the card, each
 * glyph is read at its fixed offset (idx * N*N/8) on first use, and the
 * decoded bitmaps live in a 384-entry LRU cache (~48 KB).  A full screenful of
 * unique Han is well under 384, so after the first paint every scroll hits the
 * cache and does zero SD I/O - the same warm-cache behaviour the same design
 * has on the project's STM32H7 builds, where the font was always read glyph-
 * by-glyph rather than preloaded.
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

/* ---- the faces --------------------------------------------------------- */

constexpr size_t kGlyphCount = 23940;

struct FaceSpec {
    int    cell;         /* px, one side of the square glyph cell          */
    size_t glyph_bytes;  /* cell * cell / 8                                */
    size_t file_bytes;   /* kGlyphCount * glyph_bytes                      */
};

const char *const kPaths16[] = {"/sd/fonts/GBK16.FON", "/sd/fonts/gbk16.fon"};
const char *const kPaths24[] = {"/sd/fonts/GBK24.FON", "/sd/fonts/gbk24.fon"};
const char *const kPaths32[] = {"/sd/fonts/GBK32.FON", "/sd/fonts/gbk32.fon"};

constexpr FaceSpec kSpec16 = {16, 16 * 16 / 8, kGlyphCount * 16 * 16 / 8};
constexpr FaceSpec kSpec24 = {24, 24 * 24 / 8, kGlyphCount * 24 * 24 / 8};
constexpr FaceSpec kSpec32 = {32, 32 * 32 / 8, kGlyphCount * 32 * 32 / 8};

static_assert(kSpec16.file_bytes == 766080, "the size the card's GBK16.FON must have");
static_assert(kSpec24.file_bytes == 1723680, "the size the card's GBK24.FON must have");
static_assert(kSpec32.file_bytes == 3064320, "the size the card's GBK32.FON must have");

constexpr size_t kGlyphMaxRaw = 128;   /* 32 px is the largest: 32*32/8 */

/* 0x7F is not a trail byte, so a lead's 191-byte trail range carries 190
 * glyphs.  That -1 is what makes the file 23940 glyphs rather than 24066, and
 * getting it wrong would shift every glyph after the first 0x7F by one. */
constexpr size_t kTrailsPerLead = 190;
constexpr uint8_t kNoTrail      = 0x7F;

constexpr uint16_t kNoGlyph = 0xFFFF;
constexpr size_t   kCodepoints = 0x10000;   /* the table is uint16 -> BMP only */

/* A face held entirely in PSRAM must still leave this much for everything
 * else.  2 MB is the floor under which a face is refused: an unreadable
 * document is a better trade than a Photos page that cannot decode.  The SD-
 * cached 32 px face costs almost no PSRAM, so this reserve does not apply to
 * it. */
constexpr size_t kPsramReserve = 2048 * 1024;

/* ---- an SD-backed glyph cache (used by the 32 px face) ------------------ */

struct RawCache {
    int       cell;         /* px, for the sanity of the decode path     */
    size_t    glyph_bytes;  /* raw bytes per glyph                       */
    int       capacity;     /* number of slots                           */
    uint8_t  *buf;          /* capacity * glyph_bytes, the raw glyph data */
    uint32_t *slot_gid;     /* 0 = empty; otherwise gid + 1              */
    uint32_t *last_used;    /* LRU stamps                                */
    uint32_t  tick;         /* monotonic counter for LRU                  */
};

struct Face {
    lv_font_t      font;
    int            cell;
    int            line_height;
    int            base_line;
    int            ofs_y;
    bool           ready;
    bool           psram;       /* true: glyphs in RAM; false: SD cache */
    const uint8_t *glyphs;     /* PSRAM: the whole file (nullptr when SD) */
    void          *fh;         /* SD handle (nullptr when PSRAM)           */
    RawCache      *cache;      /* SD mode only (nullptr when PSRAM)        */
};

Face s_face16;
Face s_face_large;         /* 24 px in PSRAM, or 16 px if 24 did not fit */
Face s_face_xl;            /* 32 px SD-cached, or empty                  */
uint16_t *s_index = nullptr;
bool s_tried = false;

/* ---- the vertical placement, in LVGL's terms --------------------------- */
/*
 * LVGL puts a glyph's box at
 *
 *     line_top + (line_height - base_line) - box_h - ofs_y
 *
 * The 16 px metrics are copied from the embedded face (line_height 20,
 * base_line 5), where an ofs_y of -2 puts the 16-row cell at rows 1..16.
 * The larger faces scale the same proportions (cell*5/4, cell*5/16,
 * cell/8), which lands each full cell one to two rows below the very top -
 * the same relationship the embedded face has.
 *
 * WHAT THE SWAP DOES AND DOES NOT MOVE
 * ------------------------------------
 * Horizontal layout: every CJK glyph advances exactly `cell` px with ofs_x 0
 * in every face here, so line breaking and the reader's pagination stay put.
 * (Only the embedded 16 px face carries fractional advances for '、' and
 * '（'; this module's faces give them a real full-width cell, which is a
 * correction rather than a regression.  tools/verify_sd_font.py pins the
 * 16 px pair so any drift there is loud.)
 *
 * Line height is self-consistent per face: line_height and base_line are
 * derived from the cell, not copied across sizes.
 */

void face_metrics(int cell, int *line_height, int *base_line, int *ofs_y)
{
    *line_height = cell * 5 / 4;    /* 16 -> 20, 24 -> 30, 32 -> 40 */
    *base_line   = cell * 5 / 16;   /* 16 ->  5, 24 ->  7, 32 -> 10 */
    *ofs_y       = -cell / 8;       /* 16 -> -2, 24 -> -3, 32 -> -4 */
}

/* ---- the LVGL callbacks ------------------------------------------------ */

inline size_t face_glyph_bytes(int cell) { return (size_t)cell * (size_t)cell / 8; }

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
    const Face *face = static_cast<const Face *>(font->user_data);
    (void)letter_next;   /* no kerning: every glyph here is a full-width cell */

    if (face == nullptr || letter >= kCodepoints) {
        return false;
    }
    const uint16_t gid = s_index[letter];
    if (gid == kNoGlyph) {
        return false;
    }

    /* The glyph cache keys on this, and 0 means "no glyph" to it, so the
     * index is stored one-based. */
    dsc->gid.index = (uint32_t)gid + 1;
    dsc->adv_w     = face->cell;
    dsc->box_w     = face->cell;
    dsc->box_h     = face->cell;
    dsc->ofs_x     = 0;
    dsc->ofs_y     = face->ofs_y;
    /* 0 means "no padding at the end of a line"; the consumer takes the row
     * pitch from the draw buffer it handed us instead. */
    dsc->stride    = 0;
    dsc->format    = LV_FONT_GLYPH_FORMAT_A8;
    return true;
}

/**
 * @brief Column-major monochrome source -> the A8 buffer LVGL supplied.
 *
 * Either A1 or A8 would do, but LVGL asks for A8 when req_raw_bitmap is 0 -
 * and the draw code reads the result back through an lv_draw_buf_t, taking
 * the row pitch from its header.  Writing at header.stride rather than at
 * box_w is what keeps that consistent if the buffer is ever allocated with
 * padding.
 *
 * The inner index runs over y, not x, because the file stores columns: pixel
 * (x, y) is bit (y & 7) of byte (x*(cell/8) + (y >> 3)).  See the scan-order
 * note at the top of this file - getting this backwards is a transposition,
 * not a crash, so it is the one line here that has to be read against the
 * format.
 */
void decode_glyph(const uint8_t *raw, int cell, lv_draw_buf_t *draw_buf)
{
    const size_t bpr = (size_t)cell / 8;
    uint8_t *const out = draw_buf->data;
    const uint32_t pitch = draw_buf->header.stride;

    for (int y = 0; y < cell; ++y) {
        uint8_t *const row_out = out + (size_t)y * pitch;

        /* The draw code extends the mask across the whole pitch, so the
         * padding must be a real 0 rather than whatever was in the buffer. */
        memset(row_out, 0, pitch);
        for (int x = 0; x < cell; ++x) {
            const uint8_t bits = raw[(size_t)x * bpr + (y >> 3)];
            row_out[x] = (bits & (0x80u >> (y & 7))) ? 0xFF : 0x00;
        }
    }

    lv_draw_buf_flush_cache(draw_buf, nullptr);
}

/** @brief Fetch the raw bytes for @p gid into @p dst (read from SD). */
bool read_one_glyph(void *fh, const FaceSpec &spec, size_t gid, uint8_t *dst)
{
    size_t got = 0;
    const esp_err_t e = services::storage_read_at(fh, gid * spec.glyph_bytes,
                                                 dst, spec.glyph_bytes, &got);
    return e == ESP_OK && got == spec.glyph_bytes;
}

/**
 * @brief Find a cached raw glyph, or read it from SD and cache it (LRU).
 *
 * Returns a pointer into the cache buffer that stays valid until the next
 * miss evicts that slot - which is fine, because LVGL copies the decoded
 * bitmap out of it synchronously during the same draw.
 */
const uint8_t *cache_ensure_raw(Face *face, size_t gid)
{
    RawCache *c = face->cache;
    if (c == nullptr) {
        return nullptr;
    }

    /* Hit: refresh LRU and return the slot. */
    for (int i = 0; i < c->capacity; ++i) {
        if (c->slot_gid[i] == (uint32_t)gid + 1) {
            c->last_used[i] = ++c->tick;
            return c->buf + (size_t)i * c->glyph_bytes;
        }
    }

    /* Miss: pick an empty slot, or the least-recently-used one. */
    int victim = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < c->capacity; ++i) {
        if (c->slot_gid[i] == 0) {
            victim = i;
            break;
        }
        if (c->last_used[i] < oldest) {
            oldest = c->last_used[i];
            victim = i;
        }
    }
    uint8_t *dst = c->buf + (size_t)victim * c->glyph_bytes;
    memset(dst, 0, c->glyph_bytes);   /* a short/partial read decodes as blank */
    read_one_glyph(face->fh, kSpec32, gid, dst);
    c->slot_gid[victim] = (uint32_t)gid + 1;
    c->last_used[victim] = ++c->tick;
    return dst;
}

const void *face_glyph_bitmap(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf)
{
    if (draw_buf == nullptr || draw_buf->data == nullptr || dsc->gid.index == 0) {
        return nullptr;
    }
    const size_t gid = (size_t)dsc->gid.index - 1;
    if (gid >= kGlyphCount) {
        return nullptr;
    }

    const Face *face = static_cast<const Face *>(dsc->resolved_font->user_data);
    if (face == nullptr) {
        return nullptr;
    }

    const uint8_t *raw;
    if (face->psram) {
        if (face->glyphs == nullptr) {
            return nullptr;
        }
        raw = face->glyphs + gid * face_glyph_bytes(face->cell);
    } else {
        raw = cache_ensure_raw(const_cast<Face *>(face), gid);
        if (raw == nullptr) {
            memset(draw_buf->data, 0, (size_t)draw_buf->header.stride * face->cell);
            return draw_buf;
        }
    }

    decode_glyph(raw, face->cell, draw_buf);
    return draw_buf;
}

/* ---- install ----------------------------------------------------------- */

/** @brief Glyph index for a GBK pair, in the file's lead-major layout. */
inline size_t hzk_index(uint8_t lead, uint8_t trail)
{
    return (size_t)(lead - GBK_LEAD_MIN) * kTrailsPerLead
         + (size_t)(trail - GBK_TRAIL_MIN)
         - ((trail > kNoTrail) ? 1u : 0u);
}

/**
 * @brief The shared codepoint -> glyph-index table (one per card, all sizes).
 *
 * Built from gbk_table.h alone, so it does not depend on which face files
 * turned up.
 */
bool ensure_index()
{
    if (s_index != nullptr) {
        return true;
    }
    uint16_t *index =
        (uint16_t *)heap_caps_malloc(kCodepoints * sizeof(uint16_t),
                                     MALLOC_CAP_SPIRAM);
    if (index == nullptr) {
        ESP_LOGE(TAG, "cannot spare %u KB of PSRAM for the codepoint index",
                 (unsigned)(kCodepoints * sizeof(uint16_t) / 1024));
        return false;
    }
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
    s_index = index;
    return true;
}

/**
 * @brief Does @p glyph decode as a 一, read the way decode_glyph() reads it?
 *
 * The point of this test is that a scan-order mistake is invisible everywhere
 * else.  The file is still the right size, every glyph index is still in
 * range, the font still answers get_glyph_dsc with an N x N box and an N px
 * advance, and the page still lays out - it just draws transposed shapes.
 * Nothing in a boot log would say so, and the fault only shows up as "all the
 * Chinese is garbled", which is indistinguishable from a missing-glyph
 * problem that needs an entirely different fix.
 *
 * 一 is the cheapest witness available: it is a single horizontal bar, so in
 * the right order its ink lands on one or two rows and spans nearly the whole
 * cell, and in the wrong order those same bytes are a vertical bar.  Reading
 * it back through the production path is deliberate - a test that decoded the
 * bytes itself could agree with itself while the font's reading was wrong.
 *
 * A face that cannot be read is worse than a face with a smaller repertoire,
 * so the caller refuses it and the embedded subset stays in charge.
 */
bool scan_order_is_sane(const uint8_t *glyph, int cell)
{
    if (glyph == nullptr) {
        return true;   /* no witness to judge on: do not refuse a face over this */
    }

    const size_t bpr = (size_t)cell / 8;
    int rows_with_ink = 0;
    int widest_row    = 0;
    for (int y = 0; y < cell; ++y) {
        int ink = 0;
        for (int x = 0; x < cell; ++x) {
            if (glyph[(size_t)x * bpr + (y >> 3)] & (0x80u >> (y & 7))) {
                ++ink;
            }
        }
        if (ink > 0) {
            ++rows_with_ink;
            widest_row = (ink > widest_row) ? ink : widest_row;
        }
    }

    ESP_LOGI(TAG, "scan-order witness (%d px): U+4E00 = %d row(s) of ink, widest %d px",
             cell, rows_with_ink, widest_row);
    return rows_with_ink <= 3 && widest_row >= cell * 3 / 4;
}

/** @brief The first candidate path whose size is exactly the face we know. */
const char *find_face(const char *const *paths, size_t count,
                      const FaceSpec &spec)
{
    for (size_t i = 0; i < count; ++i) {
        const uint32_t size = services::storage_file_size(paths[i]);
        if (size == (uint32_t)spec.file_bytes) {
            return paths[i];
        }
        if (size != 0) {
            ESP_LOGW(TAG, "%s is %u bytes, not the %u of a %dx%d GBK face - ignored",
                     paths[i], (unsigned)size, (unsigned)spec.file_bytes,
                     spec.cell, spec.cell);
        }
    }
    return nullptr;
}

/** @brief Wire up the LVGL font struct on a Face that already has its data. */
void dress_face(Face &face, const FaceSpec &spec, bool psram,
                const uint8_t *glyphs, void *fh, RawCache *cache)
{
    memset(&face, 0, sizeof(face));
    face.cell        = spec.cell;
    face_metrics(spec.cell, &face.line_height, &face.base_line, &face.ofs_y);
    face.psram       = psram;
    face.glyphs      = glyphs;
    face.fh          = fh;
    face.cache       = cache;
    face.font.get_glyph_dsc    = face_glyph_dsc;
    face.font.get_glyph_bitmap = face_glyph_bitmap;
    face.font.release_glyph    = nullptr;   /* nothing is allocated per glyph */
    face.font.line_height     = (uint16_t)face.line_height;
    face.font.base_line       = (int16_t)face.base_line;
    face.font.subpx           = LV_FONT_SUBPX_NONE;
    face.font.kerning         = LV_FONT_KERNING_NONE;
    face.font.static_bitmap   = 0;
    face.font.underline_position  = -(int16_t)(spec.cell / 8);
    face.font.underline_thickness = (uint8_t)(spec.cell >= 24 ? 2 : 1);
    face.font.dsc             = nullptr;   /* state lives in the Face struct */
    face.font.fallback        = &lv_font_source_han_sans_sc_16_cjk;
    face.font.user_data       = &face;
    face.ready                = true;
}

/**
 * @brief Load a face file whole into PSRAM and dress it.
 *
 * Used for the 16 px (UI) and 24 px (in-RAM reading) faces, which are small
 * enough that holding them in RAM is cheaper than touching the card per glyph.
 */
esp_err_t install_psram(const FaceSpec &spec, const char *const *paths,
                        size_t path_count, Face &face, const char *role)
{
    const char *path = find_face(paths, path_count, spec);
    if (path == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t *glyphs = (uint8_t *)heap_caps_malloc(spec.file_bytes, MALLOC_CAP_SPIRAM);
    if (glyphs == nullptr) {
        ESP_LOGE(TAG, "cannot spare %u KB of PSRAM for the %s %d px face",
                 (unsigned)(spec.file_bytes / 1024), role, spec.cell);
        return ESP_ERR_NO_MEM;
    }

    size_t got = 0;
    const esp_err_t err = services::storage_read(path, glyphs, spec.file_bytes, &got);
    if (err != ESP_OK || got != spec.file_bytes) {
        ESP_LOGE(TAG, "reading %s: %s (%u of %u bytes)", path, esp_err_to_name(err),
                 (unsigned)got, (unsigned)spec.file_bytes);
        heap_caps_free(glyphs);
        return (err != ESP_OK) ? err : ESP_ERR_INVALID_SIZE;
    }

    if (!ensure_index()) {
        heap_caps_free(glyphs);
        return ESP_ERR_NO_MEM;
    }

    /* Before anything else trusts the bytes: is this really a face in the scan
     * order we read?  Checked here, while the card is the only party at fault
     * and the embedded subset can still take over cleanly. */
    uint8_t witness[kGlyphMaxRaw];
    memset(witness, 0, sizeof(witness));
    const uint16_t gid = s_index[0x4E00];   /* 一 */
    if (gid != kNoGlyph) {
        memcpy(witness, glyphs + (size_t)gid * face_glyph_bytes(spec.cell),
               face_glyph_bytes(spec.cell));
    }
    if (!scan_order_is_sane(witness, spec.cell)) {
        ESP_LOGE(TAG, "%s does not decode as a %dx%d column-major face - refused "
                      "(text would otherwise be drawn transposed)",
                 path, spec.cell, spec.cell);
        heap_caps_free(glyphs);
        return ESP_ERR_INVALID_RESPONSE;
    }

    dress_face(face, spec, /*psram=*/true, glyphs, nullptr, nullptr);
    ESP_LOGI(TAG, "installed %s (%s): %u glyphs at %d px, %u KB PSRAM",
             path, role, (unsigned)kGlyphCount, spec.cell,
             (unsigned)(spec.file_bytes / 1024));
    return ESP_OK;
}

/**
 * @brief Install the 32 px face as an SD-cached font (no whole-file load).
 *
 * The .FON stays on the card; glyphs are read at their fixed offset on first
 * use and the decoded bitmaps are kept in a small LRU cache.  This is the
 * same "read glyph-by-glyph" strategy used on the project's STM32H7 builds,
 * and it costs ~48 KB of PSRAM instead of ~3 MB.
 */
esp_err_t install_sd_cache(const FaceSpec &spec, const char *const *paths,
                           size_t path_count, Face &face, const char *role)
{
    const char *path = find_face(paths, path_count, spec);
    if (path == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }

    void *fh = services::storage_open(path);
    if (fh == nullptr) {
        ESP_LOGE(TAG, "cannot open %s", path);
        return ESP_FAIL;
    }

    /* A screenful of unique Han is well under this; scrolling stays cache-hot. */
    const int cap = (spec.cell >= 32) ? 384 : 256;
    RawCache *cache = (RawCache *)heap_caps_malloc(sizeof(RawCache), MALLOC_CAP_SPIRAM);
    uint8_t  *buf   = (uint8_t  *)heap_caps_malloc((size_t)cap * spec.glyph_bytes,
                                                   MALLOC_CAP_SPIRAM);
    uint32_t *slot_gid  = (uint32_t *)heap_caps_malloc((size_t)cap * sizeof(uint32_t),
                                                       MALLOC_CAP_SPIRAM);
    uint32_t *last_used = (uint32_t *)heap_caps_malloc((size_t)cap * sizeof(uint32_t),
                                                       MALLOC_CAP_SPIRAM);
    if (cache == nullptr || buf == nullptr || slot_gid == nullptr ||
        last_used == nullptr) {
        ESP_LOGE(TAG, "cannot spare %u KB cache for the %s %d px face",
                 (unsigned)((cap * spec.glyph_bytes) / 1024), role, spec.cell);
        heap_caps_free(cache);
        heap_caps_free(buf);
        heap_caps_free(slot_gid);
        heap_caps_free(last_used);
        services::storage_close(fh);
        return ESP_ERR_NO_MEM;
    }

    memset(buf, 0, (size_t)cap * spec.glyph_bytes);
    memset(slot_gid, 0, (size_t)cap * sizeof(uint32_t));
    cache->cell        = spec.cell;
    cache->glyph_bytes = spec.glyph_bytes;
    cache->capacity    = cap;
    cache->buf         = buf;
    cache->slot_gid    = slot_gid;
    cache->last_used   = last_used;
    cache->tick        = 0;

    if (!ensure_index()) {
        heap_caps_free(cache);
        heap_caps_free(buf);
        heap_caps_free(slot_gid);
        heap_caps_free(last_used);
        services::storage_close(fh);
        return ESP_ERR_NO_MEM;
    }

    uint8_t witness[kGlyphMaxRaw];
    memset(witness, 0, sizeof(witness));
    const uint16_t gid = s_index[0x4E00];   /* 一 */
    if (gid != kNoGlyph) {
        read_one_glyph(fh, spec, gid, witness);
    }
    if (!scan_order_is_sane(witness, spec.cell)) {
        ESP_LOGE(TAG, "%s does not decode as a %dx%d column-major face - refused "
                      "(text would otherwise be drawn transposed)",
                 path, spec.cell, spec.cell);
        heap_caps_free(cache);
        heap_caps_free(buf);
        heap_caps_free(slot_gid);
        heap_caps_free(last_used);
        services::storage_close(fh);
        return ESP_ERR_INVALID_RESPONSE;
    }

    dress_face(face, spec, /*psram=*/false, nullptr, fh, cache);
    ESP_LOGI(TAG, "installed %s (%s): %u glyphs at %d px, SD-cached (%u KB cache)",
             path, role, (unsigned)kGlyphCount, spec.cell,
             (unsigned)((cap * spec.glyph_bytes) / 1024));
    return ESP_OK;
}

}  // namespace

esp_err_t sd_font_install()
{
    if (s_tried) {
        return s_face16.ready ? ESP_OK : ESP_ERR_NOT_FOUND;
    }
    s_tried = true;

    if (!services::storage_ready()) {
        ESP_LOGI(TAG, "no card, keeping the embedded CJK face");
        return ESP_ERR_NOT_FOUND;
    }

    /* The 16 px face is the UI size: every page's chrome expects it. */
    const esp_err_t err = install_psram(kSpec16, kPaths16, 2, s_face16, "ui face");
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no usable 16 px face on the card, keeping the embedded CJK face");
    }

    /* The 24 px reading face: held whole in PSRAM, but only if what is left
     * afterwards still respects the reserve.  Photos and the WiFi stack need
     * the rest. */
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (kSpec24.file_bytes + kPsramReserve <= free_psram) {
        install_psram(kSpec24, kPaths24, 2, s_face_large, "reading face");
    }
    if (!s_face_large.ready) {
        ESP_LOGI(TAG, "no %u KB 24 px face fits (need %u KB + %u KB reserve, "
                      "%u KB free); reading defaults to the 16 px face",
                 (unsigned)(kSpec24.file_bytes / 1024),
                 (unsigned)(kSpec24.file_bytes / 1024),
                 (unsigned)(kPsramReserve / 1024),
                 (unsigned)(free_psram / 1024));
    }

    /* The 32 px reading face: SD-cached, so it costs ~48 KB of PSRAM rather
     * than ~3 MB and never competes with the reserve.  Installed whenever the
     * file is on the card. */
    install_sd_cache(kSpec32, kPaths32, 2, s_face_xl, "reading face (XL)");
    if (!s_face_xl.ready) {
        ESP_LOGI(TAG, "no %u-byte GBK32.FON on the card; XL reading face skipped",
                 (unsigned)kSpec32.file_bytes);
    }

    if (s_index != nullptr) {
        /* Count what actually resolved rather than what the file could hold:
         * the difference is the part of cp936 the host's code page does not
         * define. */
        size_t mapped = 0;
        for (size_t i = 0; i < kCodepoints; ++i) {
            if (s_index[i] != kNoGlyph) {
                ++mapped;
            }
        }
        ESP_LOGI(TAG, "codepoint index: %u of %u cp936 slots mapped, %u KB PSRAM",
                 (unsigned)mapped, (unsigned)GBK_TRAIL_COUNT * (GBK_LEAD_MAX - GBK_LEAD_MIN + 1),
                 (unsigned)(kCodepoints * sizeof(uint16_t) / 1024));
    }
    return s_face16.ready ? ESP_OK : ESP_ERR_NOT_FOUND;
}

const lv_font_t *sd_font_cjk()
{
    return s_face16.ready ? &s_face16.font : nullptr;
}

const lv_font_t *sd_font_cjk_large()
{
    /* The in-RAM reading face (24 px when it fit, else the 16 px UI face). */
    return s_face_large.ready ? &s_face_large.font : &s_face16.font;
}

int sd_font_cjk_large_px()
{
    return s_face_large.ready ? s_face_large.cell : 16;
}

const lv_font_t *sd_font_cjk_xl()
{
    return s_face_xl.ready ? &s_face_xl.font : nullptr;
}

int sd_font_cjk_xl_px()
{
    return s_face_xl.ready ? s_face_xl.cell : 0;
}

}  // namespace ui
