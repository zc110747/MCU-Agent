/**
 * @file theme.h
 * @brief The single visual language of the application.
 *
 * Rules of engagement (see the task spec, section 35):
 *   - pages must NOT invent their own colours, radii or fonts;
 *   - every page pulls its look from here via the shared lv_style_t objects;
 *   - values are expressed in real pixels for the fixed 800x480 landscape
 *     panel, never as a scale factor applied to some other design.
 *
 * The palette is a dark, low-chroma set: one background, two surfaces, one
 * border grey and two text greys.  kAccent is the *only* chromatic colour in
 * the project and is reserved for selection / focus feedback - it is not
 * decoration.  (The Phase-1 display test page uses literal R/G/B test colours;
 * those are test signals, not theme colours, and are defined in that file.)
 */
#pragma once

#include <stdint.h>
#include "lvgl.h"

class Theme {
public:
    /* ---------------- palette ------------------------------------------ */
    static constexpr uint32_t kBg       = 0x0E1013;   /* page background     */
    static constexpr uint32_t kSurface  = 0x171A21;   /* header/footer/card  */
    static constexpr uint32_t kSurface2 = 0x1F232B;   /* pressed / raised    */
    static constexpr uint32_t kBorder   = 0x2A2F3A;
    static constexpr uint32_t kText     = 0xE6E9EF;
    static constexpr uint32_t kTextDim  = 0x9BA3B0;
    static constexpr uint32_t kAccent   = 0x4C8DFF;   /* selection / focus   */

    /* ---------------- metrics (real pixels on 800x480) ----------------- */
    static constexpr int kSafePad  = 16;              /* screen safe margin  */
    static constexpr int kGapXs    = 4;
    static constexpr int kGapSm    = 8;
    static constexpr int kGapMd    = 12;
    static constexpr int kGapLg    = 16;
    static constexpr int kGapXl    = 24;
    static constexpr int kRadiusSm = 6;
    static constexpr int kRadiusMd = 10;
    static constexpr int kRadiusLg = 14;
    static constexpr int kHeaderH  = 56;
    static constexpr int kFooterH  = 36;
    static constexpr int kTouchMin = 48;              /* min touch target    */

    /**
     * @brief Initialise the shared styles. Call once, with the LVGL lock held
     *        and before any page is created.
     */
    static void init();

    /** @brief 0xRRGGBB -> lv_color_t. */
    static lv_color_t color(uint32_t rgb) { return lv_color_hex(rgb); }

    /* ---------------- fonts ------------------------------------------- */
    static const lv_font_t *font_small();
    static const lv_font_t *font_body();
    static const lv_font_t *font_title();
    static const lv_font_t *font_h1();

    /* ---------------- shared styles (valid after init()) -------------- */
    static lv_style_t *screen();       /* page root: flat background        */
    static lv_style_t *card();         /* raised content block              */
    static lv_style_t *header();       /* top bar                           */
    static lv_style_t *footer();       /* bottom bar                        */
    static lv_style_t *divider();      /* 1 px separator line               */
    static lv_style_t *btn();          /* interactive surface               */
    static lv_style_t *btn_pressed();
    static lv_style_t *text_title();   /* label: section title              */
    static lv_style_t *text_body();    /* label: normal text                */
    static lv_style_t *text_dim();     /* label: secondary text             */
};
