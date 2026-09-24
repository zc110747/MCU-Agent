/**
 * @file theme.cpp
 * @brief Implementation of the shared LVGL styles.
 *
 * The style objects live here as file-static storage and are only touched
 * through the accessors, so a page can never mutate the shared look by
 * accident (lv_style_t is reference counted by LVGL, but the property values
 * set here are global).
 */

#include "theme.h"

#include "sd_font.h"

#include "esp_log.h"

static const char *TAG = "theme";

/* Fonts come from LVGL's Kconfig build (CONFIG_LV_FONT_MONTSERRAT_*) - Latin
 * only, so these are for chrome and any Latin text a page shows.
 *
 * CJK is a separate, heavier matter.  Exactly one CJK face is compiled into
 * the image (CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK) and handed out through
 * font_cjk().  That compiled-in face is a *subset* - 1187 Han characters - so
 * a document using anything outside it shows gaps mid-sentence.
 *
 * When the card carries a full GBK face, ui::sd_font_install() reads it into
 * PSRAM and font_cjk() hands that one out instead.  The compiled-in face stays
 * as that face's fallback, which is what keeps the swap a superset rather than
 * a trade: the card's face covers the double-byte area and nothing else, so
 * ASCII, the FontAwesome icon block LVGL's LV_SYMBOL_* lives in (U+F00D,
 * U+F013, ...), and any cp936 slot the host's code page leaves undefined are
 * all still served by the compiled-in face.  lv_font_get_glyph_dsc() walks
 * lv_font_t::fallback on a false return, so "text that rendered before still
 * renders, and text that did not, now does".
 *
 * Coverage, measured on the project's own contract doc as a body-text sample
 * (530 distinct CJK): 283 (53.4%) under the compiled-in subset, 524 (98.9%)
 * under the card's face.  The 6 that remain are emoji, in neither face.
 */

namespace {

/**
 * @brief A Latin face with a CJK face grafted on as its fallback.
 *
 * WHY THIS EXISTS
 * ---------------
 * Chinese reaches the screen by two routes.  A few pages ask for the CJK face
 * by name (Theme::font_cjk()), but far more - every list row, every key/value
 * pair, every caption, the clock's date - name one of the Montserrat faces
 * above, and those are Latin-only: lv_font_montserrat_16 has no glyph above
 * U+00FF and its fallback is NULL.  A Chinese filename drawn with it therefore
 * comes out as a row of placeholder boxes, which from the outside is
 * indistinguishable from "the file names are garbled".
 *
 * Repairing that at each call site would mean every page judging which
 * alphabet its string might contain, and the next page added would get it
 * wrong again.  So the fallback is grafted on here, once per face: the face
 * below answers from Montserrat and, when Montserrat has nothing, declines -
 * and a decline is what hands the question to lv_font_t::fallback.
 *
 * WHY A WRAPPER AND NOT JUST A fallback POINTER
 * ---------------------------------------------
 * The Montserrat fonts are `const lv_font_t` in a managed component: linked
 * into .rodata, so their `fallback` field cannot be written, and patching a
 * component's font table from application code would be the wrong place to
 * keep the knowledge anyway.
 *
 * HOW THE HAND-OFF STAYS CONSISTENT
 * ---------------------------------
 * lv_font_get_glyph_dsc() walks the chain and stamps dsc->resolved_font with
 * whichever font answered; lv_font_get_glyph_bitmap() then routes the bitmap
 * request through *that* font, not through the one that was asked.  A wrapper
 * therefore has to forward the bitmap request to the face it borrowed the
 * descriptor from - answering it locally would send a Montserrat glyph index
 * into a table the wrapper does not have.
 */
struct Face {
    lv_font_t        font;    /* what the accessors hand out                  */
    const lv_font_t *latin;   /* the stock face that answers first            */
};

bool face_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc,
              uint32_t letter, uint32_t letter_next)
{
    const Face *face = static_cast<const Face *>(font->user_data);
    if (face == nullptr) {
        return false;
    }
    const lv_font_t *latin = face->latin;
    /* Exactly what lv_font_get_glyph_dsc() would have passed had this face been
     * in the chain in its own right - including collapsing letter_next to 0
     * when the face declares that it has no kerning table. */
    return latin->get_glyph_dsc(latin, dsc, letter,
                                latin->kerning == LV_FONT_KERNING_NONE ? 0 : letter_next);
}

const void *face_bitmap(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf)
{
    const lv_font_t *answered = dsc->resolved_font;
    if (answered == nullptr || answered->user_data == nullptr) {
        return nullptr;
    }
    const Face *face = static_cast<const Face *>(answered->user_data);

    /* dsc->resolved_font has to point at the Latin face for the duration of the
     * call, and this is the one place the wrapper cannot simply forward.
     *
     * A bitmap callback takes no font argument - LVGL resolves the font from
     * dsc->resolved_font before calling, so lv_font_get_bitmap_fmt_txt() opens
     * with `const lv_font_t * font = g_dsc->resolved_font;` and then casts
     * font->dsc to its glyph table.  Left pointing at this wrapper it would read
     * the wrapper's `dsc` - which is nullptr, because the wrapper is not an
     * fmt_txt face - and fault on the first lookup.  (Observed: LoadProhibited
     * at 0x4, from lv_font_fmt_txt.c:94 with face_bitmap on the stack.)
     *
     * So the field is redirected for the call and put back afterwards, which is
     * the same save/restore lv_font_get_glyph_bitmap() performs for
     * req_raw_bitmap - a glyph that arrives through the fallback still needs
     * resolved_font to name the fallback face once this returns. */
    dsc->resolved_font = face->latin;
    const void *bitmap = face->latin->get_glyph_bitmap(dsc, draw_buf);
    dsc->resolved_font = answered;
    return bitmap;
}

void face_release(const lv_font_t *font, lv_font_glyph_dsc_t *dsc)
{
    const Face *face = static_cast<const Face *>(font->user_data);
    if (face != nullptr && face->latin->release_glyph != nullptr) {
        face->latin->release_glyph(face->latin, dsc);
    }
}

Face s_face_small{{}, &lv_font_montserrat_14};
Face s_face_body {{}, &lv_font_montserrat_16};
Face s_face_title{{}, &lv_font_montserrat_20};
Face s_face_h1   {{}, &lv_font_montserrat_28};
Face s_face_hero {{}, &lv_font_montserrat_48};

/**
 * @brief Bolt the shared callbacks and a CJK fallback onto one face.
 *
 * Every metric a layout touches is copied from the Latin face rather than from
 * the CJK one, because the Latin face is what the string was sized for: taking
 * the CJK face's 20 px line height for the 48 px clock would collapse the line.
 * The cost is that a CJK glyph inside a small label keeps its 16 px cell in a
 * 14 px line, so it can hang a pixel above the line box - visible on 14 px
 * captions only, and much cheaper than a second CJK face per size.
 */
void dress(Face &face, const lv_font_t *cjk)
{
    const lv_font_t *latin = face.latin;

    face.font.get_glyph_dsc    = face_dsc;
    face.font.get_glyph_bitmap = face_bitmap;
    face.font.release_glyph    = face_release;

    face.font.line_height         = latin->line_height;
    face.font.base_line           = latin->base_line;
    face.font.subpx               = latin->subpx;
    face.font.kerning             = latin->kerning;
    face.font.static_bitmap       = latin->static_bitmap;
    face.font.underline_position  = latin->underline_position;
    face.font.underline_thickness = latin->underline_thickness;

    face.font.dsc       = nullptr;   /* not an lv_font_fmt_txt face */
    face.font.fallback  = cjk;
    face.font.user_data = &face;     /* how the callbacks find `latin` */
}

}  // namespace

/* Re-pointed at the wrappers by init(); the stock faces here are only what a
 * caller would get if it asked before init(), which no page does. */
static const lv_font_t *s_font_small = &lv_font_montserrat_14;
static const lv_font_t *s_font_body  = &lv_font_montserrat_16;
static const lv_font_t *s_font_title = &lv_font_montserrat_20;
static const lv_font_t *s_font_h1    = &lv_font_montserrat_28;
static const lv_font_t *s_font_hero  = &lv_font_montserrat_48;
static const lv_font_t *s_font_cjk   = &lv_font_source_han_sans_sc_16_cjk;

static lv_style_t s_screen;
static lv_style_t s_card;
static lv_style_t s_header;
static lv_style_t s_footer;
static lv_style_t s_divider;
static lv_style_t s_btn;
static lv_style_t s_btn_pressed;
static lv_style_t s_tile;
static lv_style_t s_tile_pressed;
static lv_style_t s_icon_btn;
static lv_style_t s_icon_btn_pressed;
static lv_style_t s_list_row;
static lv_style_t s_list_row_pressed;
static lv_style_t s_bar_track;
static lv_style_t s_bar_indic;
static lv_style_t s_text_title;
static lv_style_t s_text_body;
static lv_style_t s_text_dim;
static lv_style_t s_text_h1;

static bool s_ready = false;

void Theme::init()
{
    if (s_ready) {
        return;
    }

    /* Which CJK face is in use is a boot-time fact (see ui/sd_font.h) and it is
     * already settled by the time the theme is built: main.cpp installs the
     * card's face as soon as the card is up.  Resolving it once, here, is what
     * lets every Latin face carry the same fallback. */
    const lv_font_t *cjk = ui::sd_font_cjk();
    if (cjk == nullptr) {
        cjk = s_font_cjk;
    }
    dress(s_face_small, cjk);
    dress(s_face_body, cjk);
    dress(s_face_title, cjk);
    dress(s_face_h1, cjk);
    dress(s_face_hero, cjk);

    s_font_small = &s_face_small.font;
    s_font_body  = &s_face_body.font;
    s_font_title = &s_face_title.font;
    s_font_h1    = &s_face_h1.font;
    s_font_hero  = &s_face_hero.font;

    /* ---- screen: flat background, no decoration ---------------------- */
    lv_style_init(&s_screen);
    lv_style_set_bg_color(&s_screen, lv_color_hex(kBg));
    lv_style_set_bg_opa(&s_screen, LV_OPA_COVER);
    lv_style_set_radius(&s_screen, 0);
    lv_style_set_border_width(&s_screen, 0);
    lv_style_set_pad_all(&s_screen, 0);
    lv_style_set_text_color(&s_screen, lv_color_hex(kText));
    lv_style_set_text_font(&s_screen, s_font_body);

    /* ---- card -------------------------------------------------------- */
    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, lv_color_hex(kSurface));
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_radius(&s_card, kRadiusMd);
    lv_style_set_border_width(&s_card, 1);
    lv_style_set_border_color(&s_card, lv_color_hex(kBorder));
    lv_style_set_pad_all(&s_card, kGapLg);
    lv_style_set_shadow_width(&s_card, 0);

    /* ---- header / footer -------------------------------------------- */
    lv_style_init(&s_header);
    lv_style_set_bg_color(&s_header, lv_color_hex(kSurface));
    lv_style_set_bg_opa(&s_header, LV_OPA_COVER);
    lv_style_set_radius(&s_header, 0);
    lv_style_set_border_width(&s_header, 0);
    lv_style_set_pad_hor(&s_header, kSafePad);
    lv_style_set_pad_ver(&s_header, 0);
    lv_style_set_shadow_width(&s_header, 0);

    lv_style_init(&s_footer);
    lv_style_set_bg_color(&s_footer, lv_color_hex(kSurface));
    lv_style_set_bg_opa(&s_footer, LV_OPA_COVER);
    lv_style_set_radius(&s_footer, 0);
    lv_style_set_border_width(&s_footer, 0);
    lv_style_set_pad_hor(&s_footer, kSafePad);
    lv_style_set_pad_ver(&s_footer, 0);
    lv_style_set_shadow_width(&s_footer, 0);

    /* ---- divider ----------------------------------------------------- */
    lv_style_init(&s_divider);
    lv_style_set_bg_color(&s_divider, lv_color_hex(kBorder));
    lv_style_set_bg_opa(&s_divider, LV_OPA_COVER);
    lv_style_set_radius(&s_divider, 0);
    lv_style_set_border_width(&s_divider, 0);
    lv_style_set_pad_all(&s_divider, 0);

    /* ---- button ------------------------------------------------------ */
    lv_style_init(&s_btn);
    lv_style_set_bg_color(&s_btn, lv_color_hex(kSurface2));
    lv_style_set_bg_opa(&s_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_btn, kRadiusSm);
    lv_style_set_border_width(&s_btn, 1);
    lv_style_set_border_color(&s_btn, lv_color_hex(kBorder));
    lv_style_set_pad_hor(&s_btn, kGapMd);
    lv_style_set_pad_ver(&s_btn, kGapSm);
    lv_style_set_shadow_width(&s_btn, 0);
    lv_style_set_text_color(&s_btn, lv_color_hex(kText));
    lv_style_set_text_font(&s_btn, s_font_body);

    lv_style_init(&s_btn_pressed);
    lv_style_set_bg_color(&s_btn_pressed, lv_color_hex(0x24314A));  /* tinted accent */
    lv_style_set_border_color(&s_btn_pressed, lv_color_hex(kAccent));

    /* ---- Home application tile --------------------------------------- */
    /* Slightly lighter than a plain card so a grid of nine of them does not
     * read as one solid slab, and it grows a 1 px accent border on press so
     * the touch target is obvious without any animation budget.           */
    lv_style_init(&s_tile);
    lv_style_set_bg_color(&s_tile, lv_color_hex(kSurface));
    lv_style_set_bg_opa(&s_tile, LV_OPA_COVER);
    lv_style_set_radius(&s_tile, kRadiusLg);
    lv_style_set_border_width(&s_tile, 1);
    lv_style_set_border_color(&s_tile, lv_color_hex(kBorder));
    lv_style_set_pad_all(&s_tile, kGapMd);
    lv_style_set_shadow_width(&s_tile, 0);

    lv_style_init(&s_tile_pressed);
    lv_style_set_bg_color(&s_tile_pressed, lv_color_hex(kSurface2));
    lv_style_set_border_width(&s_tile_pressed, 2);
    lv_style_set_border_color(&s_tile_pressed, lv_color_hex(kAccent));

    /* ---- icon-only button -------------------------------------------- */
    lv_style_init(&s_icon_btn);
    lv_style_set_bg_color(&s_icon_btn, lv_color_hex(kSurface2));
    lv_style_set_bg_opa(&s_icon_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_icon_btn, kRadiusSm);
    lv_style_set_border_width(&s_icon_btn, 1);
    lv_style_set_border_color(&s_icon_btn, lv_color_hex(kBorder));
    lv_style_set_pad_all(&s_icon_btn, 0);
    lv_style_set_shadow_width(&s_icon_btn, 0);

    lv_style_init(&s_icon_btn_pressed);
    lv_style_set_bg_color(&s_icon_btn_pressed, lv_color_hex(0x24314A));
    lv_style_set_border_color(&s_icon_btn_pressed, lv_color_hex(kAccent));

    /* ---- list row ---------------------------------------------------- */
    lv_style_init(&s_list_row);
    lv_style_set_bg_color(&s_list_row, lv_color_hex(kSurface));
    lv_style_set_bg_opa(&s_list_row, LV_OPA_COVER);
    lv_style_set_radius(&s_list_row, kRadiusSm);
    lv_style_set_border_width(&s_list_row, 1);
    lv_style_set_border_color(&s_list_row, lv_color_hex(kBorder));
    lv_style_set_pad_hor(&s_list_row, kGapMd);
    lv_style_set_pad_ver(&s_list_row, 0);
    lv_style_set_shadow_width(&s_list_row, 0);

    lv_style_init(&s_list_row_pressed);
    lv_style_set_bg_color(&s_list_row_pressed, lv_color_hex(kSurface2));
    lv_style_set_border_color(&s_list_row_pressed, lv_color_hex(kAccent));

    /* ---- progress / slider ------------------------------------------- */
    lv_style_init(&s_bar_track);
    lv_style_set_bg_color(&s_bar_track, lv_color_hex(kSurface2));
    lv_style_set_bg_opa(&s_bar_track, LV_OPA_COVER);
    lv_style_set_radius(&s_bar_track, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&s_bar_track, 0);

    lv_style_init(&s_bar_indic);
    lv_style_set_bg_color(&s_bar_indic, lv_color_hex(kAccent));
    lv_style_set_bg_opa(&s_bar_indic, LV_OPA_COVER);
    lv_style_set_radius(&s_bar_indic, LV_RADIUS_CIRCLE);

    /* ---- text -------------------------------------------------------- */
    lv_style_init(&s_text_h1);
    lv_style_set_text_font(&s_text_h1, s_font_h1);
    lv_style_set_text_color(&s_text_h1, lv_color_hex(kText));

    lv_style_init(&s_text_title);
    lv_style_set_text_font(&s_text_title, s_font_title);
    lv_style_set_text_color(&s_text_title, lv_color_hex(kText));

    lv_style_init(&s_text_body);
    lv_style_set_text_font(&s_text_body, s_font_body);
    lv_style_set_text_color(&s_text_body, lv_color_hex(kText));

    lv_style_init(&s_text_dim);
    lv_style_set_text_font(&s_text_dim, s_font_body);
    lv_style_set_text_color(&s_text_dim, lv_color_hex(kTextDim));

    s_ready = true;
    ESP_LOGI(TAG, "theme ready: bg #%06X surface #%06X text #%06X accent #%06X",
             (unsigned)kBg, (unsigned)kSurface, (unsigned)kText, (unsigned)kAccent);
}

const lv_font_t *Theme::font_small() { return s_font_small; }
const lv_font_t *Theme::font_body()  { return s_font_body; }
const lv_font_t *Theme::font_title() { return s_font_title; }
const lv_font_t *Theme::font_h1()    { return s_font_h1; }
const lv_font_t *Theme::font_hero()  { return s_font_hero; }
const lv_font_t *Theme::font_cjk()
{
    /* Resolved per call rather than captured at init, so the answer does not
     * depend on whether the card's face was installed before or after the
     * theme was built.  One branch.
     *
     * Inside a single label the two faces do mix - CJK comes from the card's
     * face, Latin and the LV_SYMBOL_* icons from the fallback - and that is
     * exactly what the fallback link is for: LVGL resolves each glyph on its
     * own and routes the bitmap through that glyph's dsc->resolved_font. */
    const lv_font_t *sd = ui::sd_font_cjk();
    return (sd != nullptr) ? sd : s_font_cjk;
}

const lv_font_t *Theme::font_cjk_large()
{
    /* Same boot-time-fact reasoning as font_cjk(): resolve per call. */
    return ui::sd_font_cjk_large();
}

int Theme::font_cjk_large_px()
{
    return ui::sd_font_cjk_large_px();
}

const lv_font_t *Theme::font_cjk_xl()
{
    /* Resolved per call, like font_cjk(): the XL face is a boot-time fact. */
    return ui::sd_font_cjk_xl();
}

int Theme::font_cjk_xl_px()
{
    return ui::sd_font_cjk_xl_px();
}

lv_style_t *Theme::screen()       { return &s_screen; }
lv_style_t *Theme::card()         { return &s_card; }
lv_style_t *Theme::header()       { return &s_header; }
lv_style_t *Theme::footer()       { return &s_footer; }
lv_style_t *Theme::divider()      { return &s_divider; }
lv_style_t *Theme::btn()          { return &s_btn; }
lv_style_t *Theme::btn_pressed()  { return &s_btn_pressed; }
lv_style_t *Theme::tile()         { return &s_tile; }
lv_style_t *Theme::tile_pressed() { return &s_tile_pressed; }
lv_style_t *Theme::icon_btn()     { return &s_icon_btn; }
lv_style_t *Theme::icon_btn_pressed() { return &s_icon_btn_pressed; }
lv_style_t *Theme::list_row()     { return &s_list_row; }
lv_style_t *Theme::list_row_pressed() { return &s_list_row_pressed; }
lv_style_t *Theme::bar_track()    { return &s_bar_track; }
lv_style_t *Theme::bar_indic()    { return &s_bar_indic; }
lv_style_t *Theme::text_title()   { return &s_text_title; }
lv_style_t *Theme::text_body()    { return &s_text_body; }
lv_style_t *Theme::text_dim()     { return &s_text_dim; }
lv_style_t *Theme::text_h1()      { return &s_text_h1; }
