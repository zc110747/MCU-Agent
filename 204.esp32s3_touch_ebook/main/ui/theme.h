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
    static constexpr uint32_t kDanger   = 0xE5534B;   /* destructive actions */
    static constexpr uint32_t kOk       = 0x3FB950;
    static constexpr uint32_t kWarn     = 0xE3B341;

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
    /* The footer hosts 48 px touch buttons; making it shorter than that clips
     * them, which is how the Calendar's "< Prev" ended up effectively hidden. */
    static constexpr int kFooterH  = 48;
    static constexpr int kTouchMin = 48;              /* min touch target    */
    static constexpr int kAppIcon  = 48;              /* Home tile glyph box */
    static constexpr int kRowH     = 48;              /* list row height     */

    /**
     * @brief Initialise the shared styles. Call once, with the LVGL lock held
     *        and before any page is created.
     */
    static void init();

    /** @brief 0xRRGGBB -> lv_color_t. */
    static lv_color_t color(uint32_t rgb) { return lv_color_hex(rgb); }

    /* ---------------- fonts ------------------------------------------- */
    static const lv_font_t *font_small();   /* 14 px - captions            */
    static const lv_font_t *font_body();    /* 16 px - normal text         */
    static const lv_font_t *font_title();   /* 20 px - section titles      */
    static const lv_font_t *font_h1();      /* 28 px - page hero           */

    /**
     * @brief 48 px, for text that is the content rather than a label.
     *
     * The clock face and the weather temperature are the two places where the
     * number *is* the page; at 28 px they read as a caption.  There is no
     * "scale this font up" path on purpose - LVGL would scale a rasterised
     * 28 px bitmap and the result would only be a blurrier 28 px font.
     */
    static const lv_font_t *font_hero();

    /**
     * @brief 16 px CJK, card's face when there is one, embedded one otherwise.
     *
     * The compiled-in face is a 1187-character subset, so it leaves gaps in
     * anything but short strings.  When /sd/fonts/GBK16.FON is present (see
     * ui/sd_font.h) the full GBK face from the card is returned instead, with
     * the compiled-in one as its fallback - ASCII and everything else keep
     * working, and the characters that used to be missing now render.
     *
     * Address the font through this function, never by binding
     * lv_font_source_han_sans_sc_16_cjk directly: which of the two is in use
     * is decided at boot, not at compile time.
     */
    static const lv_font_t *font_cjk();

    /**
     * @brief The card's large reading face (24 or 32 px), or nullptr.
     *
     * For document body text: 16 px Han is ~1.9 mm on this panel with a
     * one-pixel stroke, which reads as soft/unreadable across a whole page.
     * The card's larger HZK faces are integer bitmap cells, so they stay
     * sharp at their native size.  nullptr when no large face is on the card
     * or none fit in PSRAM - callers fall back to font_cjk().
     *
     * Address through this function, never by holding the pointer: like
     * font_cjk(), which face is in use is decided at boot.
     */
    static const lv_font_t *font_cjk_large();

    /** @brief The large face's cell size in px (for button labels), or 0. */
    static int font_cjk_large_px();

    /**
     * @brief The card's 32 px face (SD-cached, see ui/sd_font.h), or nullptr.
     *
     * Larger than font_cjk_large(), and like it intended for document body
     * text.  Served from the card on demand, so it costs almost no RAM.
     */
    static const lv_font_t *font_cjk_xl();

    /** @brief The XL face's cell size in px (32), or 0. */
    static int font_cjk_xl_px();

    /* ---------------- shared styles (valid after init()) -------------- */
    static lv_style_t *screen();       /* page root: flat background        */
    static lv_style_t *card();         /* raised content block              */
    static lv_style_t *header();       /* top bar                           */
    static lv_style_t *footer();       /* bottom bar                        */
    static lv_style_t *divider();      /* 1 px separator line               */
    static lv_style_t *btn();          /* interactive surface               */
    static lv_style_t *btn_pressed();
    static lv_style_t *tile();         /* Home application tile             */
    static lv_style_t *tile_pressed();
    static lv_style_t *icon_btn();     /* square, icon-only control         */
    static lv_style_t *icon_btn_pressed();
    static lv_style_t *list_row();     /* one row of a list                 */
    static lv_style_t *list_row_pressed();
    static lv_style_t *bar_track();    /* progress / slider groove          */
    static lv_style_t *bar_indic();    /* progress / slider fill            */
    static lv_style_t *text_title();   /* label: section title              */
    static lv_style_t *text_body();    /* label: normal text                */
    static lv_style_t *text_dim();     /* label: secondary text             */
    static lv_style_t *text_h1();      /* label: hero text                  */
};
