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

#include "esp_log.h"

static const char *TAG = "theme";

/* Fonts come from LVGL's Kconfig build (CONFIG_LV_FONT_MONTSERRAT_*) - Latin
 * only, so these are for chrome and any Latin text a page shows.
 *
 * CJK is a separate, heavier matter and is NOT loaded from storage: exactly one
 * CJK face is compiled into the image (CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK)
 * and handed out through font_cjk().  It is the single largest item in the
 * binary - roughly 1.1 MB of the ~1.24 MB image - which is why there is only
 * one size, and why the size ladder a page may offer is limited to what the
 * build actually contains rather than what a design would like.  Reader, Notes
 * and File Manager use it; nothing else should, or the ladder grows again. */
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
const lv_font_t *Theme::font_cjk()   { return s_font_cjk; }

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
