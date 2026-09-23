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
 * @brief The card's face once installed, else nullptr.
 *
 * A nullptr means "use the embedded one"; Theme::font_cjk() is the caller and
 * it owns that decision, so nothing here knows what the fallback is.
 */
const lv_font_t *sd_font_cjk();

}  // namespace ui
