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

/* Fonts come from LVGL's Kconfig build (CONFIG_LV_FONT_MONTSERRAT_*).
 * None of these carry CJK glyphs - the Reader page will mount a subset CJK
 * font from storage in a later phase and use these for chrome only. */
static const lv_font_t *s_font_small = &lv_font_montserrat_14;
static const lv_font_t *s_font_body  = &lv_font_montserrat_16;
static const lv_font_t *s_font_title = &lv_font_montserrat_20;
static const lv_font_t *s_font_h1    = &lv_font_montserrat_28;

static lv_style_t s_screen;
static lv_style_t s_card;
static lv_style_t s_header;
static lv_style_t s_footer;
static lv_style_t s_divider;
static lv_style_t s_btn;
static lv_style_t s_btn_pressed;
static lv_style_t s_text_title;
static lv_style_t s_text_body;
static lv_style_t s_text_dim;

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

    /* ---- text -------------------------------------------------------- */
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

lv_style_t *Theme::screen()       { return &s_screen; }
lv_style_t *Theme::card()         { return &s_card; }
lv_style_t *Theme::header()       { return &s_header; }
lv_style_t *Theme::footer()       { return &s_footer; }
lv_style_t *Theme::divider()      { return &s_divider; }
lv_style_t *Theme::btn()          { return &s_btn; }
lv_style_t *Theme::btn_pressed()  { return &s_btn_pressed; }
lv_style_t *Theme::text_title()   { return &s_text_title; }
lv_style_t *Theme::text_body()    { return &s_text_body; }
lv_style_t *Theme::text_dim()     { return &s_text_dim; }
