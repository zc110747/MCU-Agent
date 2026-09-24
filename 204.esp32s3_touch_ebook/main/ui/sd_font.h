/**
 * @file sd_font.h
 * @brief The GBK face that lives on the card, as an LVGL font.
 *
 * WHY THIS EXISTS
 * ---------------
 * The CJK face compiled into the image (CONFIG_LV_FONT_SOURCE_HAN_SANS_SC_16_CJK)
 * is a *hand-picked subset*, not a Chinese font: 1434 glyphs total, of which
 * 1187 are Han.  Anything the document uses beyond that set has no glyph and
 * the reader shows a gap mid-sentence.
 *
 * A card prepared for this device ships /sd/fonts/{GBK12,GBK16,GBK24,GBK32}.FON
 * - classic HZK-layout GBK bitmap faces that cover the whole GBK set.  When one
 * is there, this module installs it as the CJK face; when it is not, the
 * embedded subset stays and nothing changes.  The check is a file size, so a
 * card carrying something else at that path is refused rather than
 * misread (see kFileBytes in the implementation).
 *
 * WHAT IT COSTS
 * -------------
 * 766 KB of PSRAM for the glyphs plus 128 KB for the codepoint index, both
 * allocated once and held for the lifetime of the program.  Pulling the card
 * afterwards does not matter: the face is already in RAM.
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

namespace ui {

/**
 * @brief Install the card's GBK face, if the card has one.
 *
 * Idempotent.  Reads the whole file, so it must NOT be called with the LVGL
 * lock held - it is pure filesystem and heap work and takes a noticeable
 * fraction of a second.
 *
 * @return ESP_OK            the card's face is now the CJK face
 *         ESP_ERR_NOT_FOUND the card has no usable face (a normal state)
 *         ESP_ERR_NO_MEM    the face is there but did not fit in PSRAM
 *         other             the card could not be read
 */
esp_err_t sd_font_install();

/**
 * @brief The card's 16 px face once installed, else nullptr.
 *
 * A nullptr means "use the embedded one"; Theme::font_cjk() is the caller and
 * it owns that decision, so nothing here knows what the fallback is.
 *
 * This is the chrome/UI size.  For document body text see sd_font_cjk_large().
 */
const lv_font_t *sd_font_cjk();

/**
 * @brief The card's 24 px face when it fit in PSRAM (the "large" reading size),
 *        else the 16 px face.  Used as the in-RAM reading size.
 *
 * 16 px Han is ~1.9 mm on this panel with a one-pixel stroke - right for
 * labels, too small and too thin for a page of body text.  The card ships
 * proper larger HZK faces; an integer bitmap cell at its native size is
 * perfectly sharp, unlike a scaled outline font, which only blurs.
 *
 * The 24 px face is held entirely in PSRAM (it is moderate: 1723680 bytes).
 * When it does not fit the PSRAM reserve, this returns the 16 px face so the
 * caller still has something.
 */
const lv_font_t *sd_font_cjk_large();

/** @brief The large face's cell size in px (24 or 16), or 0 when there is none. */
int sd_font_cjk_large_px();

/**
 * @brief The card's 32 px face, served from the SD card on demand through a
 *        small decoded-glyph cache, else nullptr.
 *
 * Unlike the 24 px face, the 32 px face is NOT loaded into PSRAM: at 3064320
 * bytes it would waste most of the RAM budget for a size that is only one of
 * several the reader offers.  Instead the .FON stays on the card, each glyph
 * is read at its fixed offset on first use, and the decoded bitmaps are kept in
 * a 384-entry cache (about 48 KB) - enough for a whole screenful of unique
 * Han, so steady-state scrolling hits the cache and does no SD I/O at all.
 */
const lv_font_t *sd_font_cjk_xl();

/** @brief The XL face's cell size in px (32), or 0 when there is none. */
int sd_font_cjk_xl_px();

}  // namespace ui
