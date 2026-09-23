/**
 * @file reader_page.cpp
 * @brief Phase 5: open a TXT file from the card and read it page by page.
 *
 * PAGINATION WITHOUT MEASURING ANY TEXT
 * -------------------------------------
 * The obvious implementation - split the file into lines, measure each one with
 * lv_txt_get_size(), and pack them into pages - means re-implementing the
 * label's own line breaking and hoping the two agree about every space, hyphen
 * and CJK line-break opportunity.  When they disagree the last line of a page is
 * clipped.
 *
 * So instead: the whole book goes into one wrapping label inside a
 * fixed-height, clipped viewport, and "next page" scrolls the viewport by
 * exactly one viewport height.  The visible result is identical to pagination,
 * the line breaking is done by the code whose job it is, and the progress
 * indicator is derived from the scroll position - which is the ground truth
 * rather than an estimate.
 *
 * ENCODING
 * --------
 * Files are converted to UTF-8 on load (see text_service), so a GBK file saved
 * by Windows Notepad reads correctly.  The font ladder includes a CJK face
 * because there is no system font on this chip: without one, Chinese renders as
 * empty boxes and the page would look broken rather than misconfigured.
 */

#include "reader_page.h"

#include <stdio.h>
#include <string.h>

#include "app_icons.h"
#include "app_manager.h"
#include "theme.h"
#include "widgets.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "reader";

namespace {

/**
 * @brief The font ladder.
 *
 * Built lazily so that it does not depend on the initialisation order of the
 * Theme's static pointers at namespace scope.
 */
struct ReaderFont {
    const char      *label;
    const lv_font_t *font;
    const char      *note;
};

const ReaderFont *fonts(size_t *count)
{
    static const ReaderFont kFonts[] = {
        {"A14", nullptr, "Latin, 14 px"},
        {"A16", nullptr, "Latin, 16 px"},
        {"A20", nullptr, "Latin, 20 px"},
        {"A28", nullptr, "Latin, 28 px"},
        {"CN",  nullptr, "Chinese + Latin, 16 px"},
    };
    static ReaderFont resolved[sizeof(kFonts) / sizeof(kFonts[0])];
    static bool done = false;

    *count = sizeof(kFonts) / sizeof(kFonts[0]);
    if (!done) {
        resolved[0].label = kFonts[0].label;
        resolved[0].font = Theme::font_small();
        resolved[0].note = kFonts[0].note;
        resolved[1].label = kFonts[1].label;
        resolved[1].font = Theme::font_body();
        resolved[1].note = kFonts[1].note;
        resolved[2].label = kFonts[2].label;
        resolved[2].font = Theme::font_title();
        resolved[2].note = kFonts[2].note;
        resolved[3].label = kFonts[3].label;
        resolved[3].font = Theme::font_h1();
        resolved[3].note = kFonts[3].note;
        resolved[4].label = kFonts[4].label;
        resolved[4].font = Theme::font_cjk();
        resolved[4].note = kFonts[4].note;
        done = true;
    }
    return resolved;
}

/* The CJK face is the default: it is the only one that can render a Chinese
 * file, and it renders Latin acceptably, so starting anywhere else would mean
 * the first thing a user opens looks broken. */
constexpr int kDefaultFontIndex = 4;

}  // namespace

/* ------------------------------------------------------------------------ */
/* shell                                                                    */
/* ------------------------------------------------------------------------ */

void ReaderPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Reader", true);
    root_ = page.root;

    body_ = page.body;
    header_slot_ = page.header_right;
    footer_left_ = page.footer_left;
    footer_right_ = page.footer_right;

    font_index_ = kDefaultFontIndex;
    build_list();

    ESP_LOGI(TAG, "reader built");
}

void ReaderPage::destroy()
{
    close_book();
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    body_ = nullptr;
    header_slot_ = nullptr;
    footer_left_ = nullptr;
    footer_right_ = nullptr;
    list_ = nullptr;
    viewport_ = nullptr;
    text_ = nullptr;
    font_btn_ = nullptr;
    meta_label_ = nullptr;
    progress_label_ = nullptr;
}

void ReaderPage::on_enter()
{
    if (mode_ == Mode::List) {
        load_list();
    }
}

void ReaderPage::close_book()
{
    services::text_free(&doc_);
    open_name_[0] = '\0';
}

/* ------------------------------------------------------------------------ */
/* list mode                                                                */
/* ------------------------------------------------------------------------ */

void ReaderPage::build_list()
{
    mode_ = Mode::List;
    if (body_ == nullptr) {
        return;
    }
    lv_obj_clean(body_);
    lv_obj_clean(header_slot_);
    lv_obj_clean(footer_left_);
    lv_obj_clean(footer_right_);

    list_ = lv_obj_create(body_);
    lv_obj_remove_style_all(list_);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, Theme::kGapSm, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *hint = lv_label_create(header_slot_);
    lv_obj_add_style(hint, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(hint, Theme::font_small(), 0);
    lv_label_set_text(hint, kBookDir);

    progress_label_ = lv_label_create(footer_left_);
    lv_obj_add_style(progress_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(progress_label_, Theme::font_small(), 0);
    lv_label_set_text(progress_label_, "--");

    load_list();
}

void ReaderPage::load_list()
{
    if (list_ == nullptr) {
        return;
    }
    lv_obj_clean(list_);
    entry_count_ = 0;
    listed_total_ = 0;

    if (!services::storage_ready()) {
        ui::empty_state(list_, "--", "No SD card",
                        "TXT files are read from " "/sd/Ebook/txt" ". "
                        "Insert a card and reopen this page.");
        if (progress_label_ != nullptr) {
            lv_label_set_text(progress_label_, "no card");
        }
        return;
    }

    /* Try the specified folder first, then the folder the rest of the card's
     * content lives in, then the card root; a card prepared by hand often just
     * has the files at the top level. */
    const char *dirs[3] = {kBookDir, kEbookDir, kFallbackDir};
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (const char *dir : dirs) {
        err = services::storage_list(dir, entries_, services::kMaxEntriesPerPage,
                                    &listed_total_);
        if (err == ESP_OK) {
            break;
        }
    }

    if (err != ESP_OK) {
        ui::empty_state(list_, "--", "Cannot read the card",
                        "None of /sd/Ebook/txt, /sd/Ebook or /sd could be listed.");
        return;
    }

    /* Text files only.  A reader that lists JPEGs it cannot open is worse than
     * one that lists nothing. */
    size_t shown = 0;
    for (size_t i = 0; i < listed_total_ && i < services::kMaxEntriesPerPage; ++i) {
        const services::DirEntry &e = entries_[i];
        if (e.is_dir || !services::storage_is_text(e.name)) {
            continue;
        }

        char size[24];
        services::storage_human_size(size, sizeof(size), e.size);

        lv_obj_t *row = ui::list_item(list_, &icon_reader, e.name, size,
                                      true, row_cb, this);
        lv_obj_set_user_data(row, reinterpret_cast<void *>(i + 1));
        ++shown;
    }
    entry_count_ = shown;

    if (shown == 0) {
        ui::empty_state(list_, "--", "No TXT files found",
                        "Put .txt / .md / .log files in /sd/Ebook/txt. "
                        "Both UTF-8 and GBK are handled.");
    }

    if (progress_label_ != nullptr) {
        lv_label_set_text_fmt(progress_label_, "%u book%s", (unsigned)shown,
                              shown == 1 ? "" : "s");
    }
    ESP_LOGI(TAG, "listed %u readable files", (unsigned)shown);
}

/* ------------------------------------------------------------------------ */
/* reading mode                                                             */
/* ------------------------------------------------------------------------ */

void ReaderPage::open_book(const char *filename)
{
    char path[256];

    /* The listing searched these directories in this order, so walk them again
     * here: a joined path is only correct for the directory the file was
     * actually found in. */
    const char *dirs[3] = {kBookDir, kEbookDir, kFallbackDir};
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (const char *dir : dirs) {
        services::storage_join(path, sizeof(path), dir, filename);
        err = services::text_load_file(path, &doc_);
        if (err != ESP_ERR_NOT_FOUND) {
            break;
        }
    }

    if (err != ESP_OK) {
        close_book();
        snprintf(open_name_, sizeof(open_name_), "%s", filename);

        mode_ = Mode::Reading;
        build_reading();

        /* Report the reason per case.  "Cannot open" would hide the difference
         * between a missing file, an oversized one and an encoding this build
         * cannot read - three problems with three different answers. */
        const char *title = "Cannot open this file";
        char detail[160];
        switch (err) {
        case ESP_ERR_NOT_FOUND:
            snprintf(detail, sizeof(detail), "The file disappeared from the card.");
            break;
        case ESP_ERR_INVALID_SIZE:
            snprintf(detail, sizeof(detail),
                     "Empty, or larger than the %u KB limit this build reads.",
                     (unsigned)(services::kTextMaxBytes / 1024));
            break;
        case ESP_ERR_NO_MEM:
            snprintf(detail, sizeof(detail), "Not enough memory to hold the text.");
            break;
        default:
            title = "Unsupported encoding";
            snprintf(detail, sizeof(detail),
                     "The bytes are neither valid UTF-8 nor usable GBK, so there is "
                     "nothing this build can render. Re-save the file as UTF-8.");
            break;
        }

        /* The viewport already holds the (empty) reading label; replace it so
         * the reader can still use List / Back from the header. */
        lv_obj_clean(viewport_);
        text_ = nullptr;
        ui::empty_state(viewport_, "--", title, detail);
        if (meta_label_ != nullptr) {
            lv_label_set_text(meta_label_, filename);
        }
        if (progress_label_ != nullptr) {
            lv_label_set_text(progress_label_, "not loaded");
        }
        return;
    }

    snprintf(open_name_, sizeof(open_name_), "%s", filename);
    mode_ = Mode::Reading;
    build_reading();
    lv_label_set_text(text_, doc_.utf8);
    apply_font();

    char desc[64];
    services::text_describe(&doc_, desc, sizeof(desc));
    if (meta_label_ != nullptr) {
        lv_label_set_text(meta_label_, desc);
    }

    ESP_LOGI(TAG, "opened %s: %s", filename, desc);
}

void ReaderPage::build_reading()
{
    if (body_ == nullptr) {
        return;
    }
    lv_obj_clean(body_);
    lv_obj_clean(header_slot_);
    lv_obj_clean(footer_left_);
    lv_obj_clean(footer_right_);

    /* Header: font ladder, page turns, and a way back to the list. */
    size_t font_count = 0;
    const ReaderFont *f = fonts(&font_count);
    font_btn_ = ui::app_button(header_slot_, f[font_index_].label, font_cb, this);
    ui::app_button(header_slot_, "Prev", prev_cb, this);
    ui::app_button(header_slot_, "Next", next_cb, this);
    ui::app_button(header_slot_, "List", list_cb, this);

    /* The viewport is the clip window; the label inside it is the whole book.
     * Scrolling this object by one viewport height *is* a page turn. */
    viewport_ = lv_obj_create(body_);
    lv_obj_remove_style_all(viewport_);
    lv_obj_add_style(viewport_, Theme::card(), 0);
    lv_obj_set_width(viewport_, LV_PCT(100));
    lv_obj_set_flex_grow(viewport_, 1);
    lv_obj_set_scroll_dir(viewport_, LV_DIR_VER);
    /* No visible bar: the progress line already says where we are, and a bar
     * over the text is just clutter on a small panel. */
    lv_obj_set_scrollbar_mode(viewport_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(viewport_, Theme::kGapMd, 0);

    text_ = lv_label_create(viewport_);
    lv_obj_set_width(text_, LV_PCT(100));
    lv_obj_set_style_text_color(text_, lv_color_hex(Theme::kText), 0);
    lv_label_set_long_mode(text_, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(text_, "");

    meta_label_ = lv_label_create(footer_left_);
    lv_obj_add_style(meta_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(meta_label_, Theme::font_small(), 0);
    lv_label_set_text(meta_label_, "--");

    progress_label_ = lv_label_create(footer_left_);
    lv_obj_add_style(progress_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(progress_label_, Theme::font_small(), 0);
    lv_label_set_text(progress_label_, "page 1/1");
}

void ReaderPage::apply_font()
{
    if (text_ == nullptr) {
        return;
    }
    size_t font_count = 0;
    const ReaderFont *f = fonts(&font_count);
    if (font_index_ < 0 || (size_t)font_index_ >= font_count) {
        font_index_ = 0;
    }
    lv_obj_set_style_text_font(text_, f[font_index_].font, 0);

    if (font_btn_ != nullptr) {
        lv_obj_t *label = lv_obj_get_child(font_btn_, 0);
        if (label != nullptr) {
            lv_label_set_text(label, f[font_index_].label);
        }
    }

    /* Changing the font re-flows every line, so the old scroll offset points at
     * unrelated text.  Going back to the top is the only honest option; keeping
     * the offset would silently jump the reader somewhere else in the book. */
    if (viewport_ != nullptr) {
        lv_obj_scroll_to_y(viewport_, 0, LV_ANIM_OFF);
    }
    refresh_progress();
}

void ReaderPage::turn_page(int direction)
{
    if (viewport_ == nullptr) {
        return;
    }
    const lv_coord_t h = lv_obj_get_height(viewport_);
    if (h <= 0) {
        return;
    }
    /* scroll_by_bounded() clamps at both ends, so holding Next at the last page
     * is a no-op instead of an over-scroll the user has to drag back. */
    lv_obj_scroll_by_bounded(viewport_, 0, (int32_t)(-direction * h), LV_ANIM_OFF);
    refresh_progress();
}

void ReaderPage::refresh_progress()
{
    if (viewport_ == nullptr || progress_label_ == nullptr) {
        return;
    }

    const int32_t vp_h = lv_obj_get_height(viewport_);
    if (vp_h <= 0) {
        return;
    }

    /* The ground truth is the scroll position, not a page counter we maintain
     * ourselves - so the number on screen cannot drift from what is displayed. */
    const int32_t y = lv_obj_get_scroll_y(viewport_);
    const int32_t bottom = lv_obj_get_scroll_bottom(viewport_);
    const int32_t range = y + bottom;

    if (range <= 0) {
        lv_label_set_text(progress_label_, "all text fits on one page");
        return;
    }

    const int page = (int)((y + vp_h - 1) / vp_h) + 1;
    const int pages = (int)((range + vp_h - 1) / vp_h) + 1;
    const int percent = (int)((y * 100) / range);

    lv_label_set_text_fmt(progress_label_, "page %d/%d   %d%%", page, pages, percent);
}

/* ------------------------------------------------------------------------ */
/* events                                                                   */
/* ------------------------------------------------------------------------ */

void ReaderPage::row_cb(lv_event_t *e)
{
    ReaderPage *self = static_cast<ReaderPage *>(lv_event_get_user_data(e));
    lv_obj_t *row = lv_event_get_target_obj(e);
    if (self == nullptr || row == nullptr) {
        return;
    }
    const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
    if (tag <= 0 || (size_t)tag > services::kMaxEntriesPerPage) {
        return;
    }
    self->open_book(self->entries_[tag - 1].name);
}

void ReaderPage::font_cb(lv_event_t *e)
{
    ReaderPage *self = static_cast<ReaderPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    size_t count = 0;
    fonts(&count);
    self->font_index_ = (self->font_index_ + 1) % (int)count;
    self->apply_font();
}

void ReaderPage::prev_cb(lv_event_t *e)
{
    ReaderPage *self = static_cast<ReaderPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->turn_page(-1);
    }
}

void ReaderPage::next_cb(lv_event_t *e)
{
    ReaderPage *self = static_cast<ReaderPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->turn_page(1);
    }
}

void ReaderPage::list_cb(lv_event_t *e)
{
    ReaderPage *self = static_cast<ReaderPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->close_book();
    self->build_list();
}
