/**
 * @file notes_page.cpp
 * @brief Phase 8: a plain-text scratchpad on the SD card.
 *
 * Two modes in one page
 * ---------------------
 * Browsing and editing are states of the same page rather than two entries in
 * the navigation stack.  That is not a shortcut: opening a note from a separate
 * "editor page" would mean passing a filename into a page, and nothing else in
 * this application needs parameterised pages.  Keeping both modes here keeps
 * every page's constructor argument-free and makes "unsaved changes" a local
 * question instead of a cross-page one.
 *
 * Chinese input
 * -------------
 * LVGL ships a keyboard, not an IME - there is no pinyin engine to call, and the
 * panel has no system input method.  Rather than pretend, this page offers a
 * one-tap strip of the most frequent simplified characters next to the Latin
 * keyboard.  It is genuinely usable for short notes (and is how Chinese was
 * entered on phones before predictive input), and it is also the seam a real
 * IME would fill: replace kCnQuickChars with whatever the engine suggests for
 * the current pre-edit string and the rest of the page is unchanged.
 */

#include "notes_page.h"

#include <stdio.h>
#include <string.h>

#include "app_icons.h"
#include "app_manager.h"
#include "clock_service.h"
#include "text_service.h"
#include "theme.h"
#include "widgets.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "notes";

namespace {

constexpr uint32_t kArmTimeoutMs = 4000;
constexpr int kKeyboardH = 190;
constexpr int kCnStripH = 48;

/**
 * @brief One-tap Chinese characters, ordered by frequency in running text.
 *
 * Kept deliberately short: a strip nobody scrolls is not a candidate bar.  When
 * a real IME lands it produces this list from the pre-edit string and this
 * constant disappears.
 */
const char *const kCnQuickChars[] = {
    "的", "一", "是", "不", "了", "在", "人", "有", "我", "他",
    "这", "中", "大", "来", "上", "国", "个", "到", "说", "时",
    "地", "要", "就", "出", "会", "可", "也", "你", "对", "生",
    "好", "天", "年", "月", "日", "星", "期", "测", "试", "文",
};
constexpr size_t kCnQuickCount = sizeof(kCnQuickChars) / sizeof(kCnQuickChars[0]);

/** @brief Build a filename from the current time, e.g. note_20260922_134500.txt. */
void make_note_name(char *dst, size_t len)
{
    services::TimeParts t;
    services::clock_now(&t);
    snprintf(dst, len, "note_%04d%02d%02d_%02d%02d%02d.txt",
             t.year, t.month, t.day, t.hour, t.minute, t.second);
}

}  // namespace

/* ------------------------------------------------------------------------ */
/* shell                                                                    */
/* ------------------------------------------------------------------------ */

void NotesPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Notes", true);
    root_ = page.root;

    body_ = page.body;
    header_slot_ = page.header_right;
    footer_left_ = page.footer_left;
    footer_right_ = page.footer_right;

    set_mode(Mode::List);

    ESP_LOGI(TAG, "notes built");
}

void NotesPage::destroy()
{
    if (arm_timer_ != nullptr) {
        lv_timer_delete(arm_timer_);
        arm_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    body_ = nullptr;
    header_slot_ = nullptr;
    footer_left_ = nullptr;
    footer_right_ = nullptr;
    list_ = nullptr;
    textarea_ = nullptr;
    keyboard_ = nullptr;
    cn_strip_ = nullptr;
    note_label_ = nullptr;
    armed_ = -1;
}

void NotesPage::on_enter()
{
    if (mode_ == Mode::List) {
        load_list();
    }
}

void NotesPage::on_leave()
{
    if (arm_timer_ != nullptr) {
        lv_timer_delete(arm_timer_);
        arm_timer_ = nullptr;
    }
    armed_ = -1;
}

void NotesPage::set_mode(Mode m)
{
    mode_ = m;

    /* Wipe both the body and the header/footer slots: a mode change replaces
     * the page's entire interactive surface, and leaving stale buttons behind
     * is how a "Save" ends up saving the wrong thing. */
    if (body_ != nullptr) {
        lv_obj_clean(body_);
    }
    if (header_slot_ != nullptr) {
        lv_obj_clean(header_slot_);
    }
    if (footer_left_ != nullptr) {
        lv_obj_clean(footer_left_);
    }
    if (footer_right_ != nullptr) {
        lv_obj_clean(footer_right_);
    }
    list_ = nullptr;
    textarea_ = nullptr;
    keyboard_ = nullptr;
    cn_strip_ = nullptr;
    note_label_ = nullptr;
    cn_on_ = false;
    armed_ = -1;

    if (m == Mode::List) {
        build_list();
    } else {
        build_editor();
    }
}

/* ------------------------------------------------------------------------ */
/* list mode                                                                */
/* ------------------------------------------------------------------------ */

void NotesPage::build_list()
{
    if (body_ == nullptr) {
        return;
    }

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
    lv_label_set_text(hint, kDir);

    note_label_ = lv_label_create(footer_left_);
    lv_obj_add_style(note_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(note_label_, Theme::font_small(), 0);
    lv_label_set_text(note_label_, "--");

    ui::app_button(footer_left_, "New", new_cb, this);
    ui::app_button(footer_right_, "Back", back_cb, nullptr);

    load_list();
}

void NotesPage::load_list()
{
    if (list_ == nullptr) {
        return;
    }
    lv_obj_clean(list_);
    armed_ = -1;

    if (!services::storage_ready()) {
        ui::empty_state(list_, "--", "No SD card",
                        "Notes are stored on the card at " "/sd/notes" ". "
                        "Insert a card and reopen this page.");
        if (note_label_ != nullptr) {
            lv_label_set_text(note_label_, "no card");
        }
        return;
    }

    /* Create the folder on first use rather than at boot, so a card that is
     * inserted later still works without a restart. */
    services::storage_mkdir(kDir);

    const esp_err_t err = services::storage_list(kDir, entries_,
                                                 services::kMaxEntriesPerPage,
                                                 &listed_total_);
    entry_count_ = (err == ESP_OK) ? listed_total_ : 0;
    if (entry_count_ > services::kMaxEntriesPerPage) {
        entry_count_ = services::kMaxEntriesPerPage;
    }

    if (err != ESP_OK) {
        ui::empty_state(list_, "--", "Cannot list /sd/notes",
                        "The folder could not be read. Check the card.");
        return;
    }

    for (size_t i = 0; i < entry_count_; ++i) {
        const services::DirEntry &e = entries_[i];
        if (e.is_dir) {
            continue;   /* flat folder, by design */
        }

        char size[24];
        services::storage_human_size(size, sizeof(size), e.size);

        lv_obj_t *row = ui::list_item(list_, &icon_notes, e.name, size,
                                      false, row_cb, this);
        lv_obj_set_user_data(row, reinterpret_cast<void *>(i + 1));

        lv_obj_t *bin = ui::icon_button(row, &icon_trash, 40, trash_cb, this);
        lv_obj_set_user_data(bin, reinterpret_cast<void *>(i + 1));
    }

    if (entry_count_ == 0) {
        ui::empty_state(list_, "--", "No notes yet",
                        "Press New to start one. Files are stored as plain text "
                        "in /sd/notes, so they open anywhere.");
    }

    if (note_label_ != nullptr) {
        /* The directory is spelled out rather than concatenated from kDir: the
         * compiler only joins adjacent *literals*, so pasting a constexpr
         * pointer into a format string is a compile error, not a shortcut. */
        lv_label_set_text_fmt(note_label_, "%u note%s in /sd/notes",
                              (unsigned)entry_count_, entry_count_ == 1 ? "" : "s");
    }
    ESP_LOGI(TAG, "listed %u notes (%u present)", (unsigned)entry_count_,
             (unsigned)listed_total_);
}

/* ------------------------------------------------------------------------ */
/* editor mode                                                              */
/* ------------------------------------------------------------------------ */

void NotesPage::build_editor()
{
    if (body_ == nullptr) {
        return;
    }

    /* Save first in the header, so the primary action is not buried next to
     * Close in the footer. */
    ui::app_button(header_slot_, "Save", save_cb, this);
    ui::app_button(header_slot_, "\xE4\xB8\xAD", cn_toggle_cb, this);   /* U+4E2D */

    lv_obj_t *ta = lv_textarea_create(body_);
    lv_obj_remove_style_all(ta);
    /* Textarea is one of the few widgets whose default theme look is worth
     * keeping - a text cursor and a visible frame - so only the colours and the
     * geometry are overridden, not the behaviour. */
    lv_obj_add_style(ta, Theme::card(), 0);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_flex_grow(ta, 1);
    lv_textarea_set_one_line(ta, false);
    lv_textarea_set_placeholder_text(ta, "Type here...");
    lv_obj_set_style_text_font(ta, Theme::font_cjk(), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(Theme::kText), 0);
    lv_obj_set_style_bg_color(ta, lv_color_hex(Theme::kSurface), LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(Theme::kBorder), LV_PART_MAIN);
    lv_obj_set_style_pad_all(ta, Theme::kGapMd, LV_PART_MAIN);
    textarea_ = ta;

    /* The candidate strip is created hidden; the 中 button shows it. */
    cn_strip_ = lv_obj_create(body_);
    lv_obj_remove_style_all(cn_strip_);
    lv_obj_set_width(cn_strip_, LV_PCT(100));
    lv_obj_set_height(cn_strip_, kCnStripH);
    lv_obj_set_flex_flow(cn_strip_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cn_strip_, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(cn_strip_, 4, 0);
    lv_obj_set_scroll_dir(cn_strip_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(cn_strip_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(cn_strip_, LV_OBJ_FLAG_HIDDEN);

    for (size_t i = 0; i < kCnQuickCount; ++i) {
        lv_obj_t *b = lv_obj_create(cn_strip_);
        lv_obj_remove_style_all(b);
        lv_obj_add_style(b, Theme::icon_btn(), 0);
        lv_obj_add_style(b, Theme::icon_btn_pressed(), LV_STATE_PRESSED);
        lv_obj_set_size(b, 44, 40);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_text_font(b, Theme::font_cjk(), 0);

        lv_obj_t *label = lv_label_create(b);
        lv_obj_set_style_text_font(label, Theme::font_cjk(), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(Theme::kText), 0);
        lv_label_set_text(label, kCnQuickChars[i]);
        lv_obj_center(label);

        lv_obj_add_event_cb(b, cn_char_cb, LV_EVENT_CLICKED, this);
        /* The character rides on the object; the page on the event. */
        lv_obj_set_user_data(b, const_cast<char *>(kCnQuickChars[i]));
    }

    lv_obj_t *kb = lv_keyboard_create(body_);
    lv_obj_set_width(kb, LV_PCT(100));
    lv_obj_set_height(kb, kKeyboardH);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    /* The keyboard's own OK key would otherwise be a dead end: it emits
     * LV_EVENT_READY and nothing would happen.  Wiring it to Save makes the
     * most natural key on the panel do the most expected thing. */
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_READY, this);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_VALUE_CHANGED, this);
    keyboard_ = kb;

    /* Typing marks the note dirty so Close can warn before losing edits. */
    lv_obj_add_event_cb(ta, kb_cb, LV_EVENT_VALUE_CHANGED, this);

    /* The editor gets its own colours; the keyboard inherits the dark theme from
     * its parent chain, with only the key surface adjusted. */
    lv_obj_set_style_bg_color(kb, lv_color_hex(Theme::kSurface), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb, lv_color_hex(Theme::kSurface2), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(Theme::kText), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, Theme::kRadiusSm, LV_PART_ITEMS);

    note_label_ = lv_label_create(footer_left_);
    lv_obj_add_style(note_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(note_label_, Theme::font_small(), 0);
    lv_label_set_text(note_label_, open_name_[0] ? open_name_ : "new note (not saved)");

    ui::app_button(footer_right_, "Close", close_cb, this);

    if (open_name_[0] != '\0') {
        char path[256];
        services::storage_join(path, sizeof(path), kDir, open_name_);
        services::TextDoc doc;
        /* Notes are written by this page as UTF-8, but a file could have been
         * dropped onto the card by a PC, so it goes through the same conversion
         * the Reader uses. */
        if (services::text_load_file(path, &doc) == ESP_OK) {
            lv_textarea_set_text(ta, doc.utf8);
        }
        services::text_free(&doc);
    }

    lv_keyboard_set_textarea(kb, ta);
    dirty_ = false;
}

/* ------------------------------------------------------------------------ */
/* actions                                                                  */
/* ------------------------------------------------------------------------ */

void NotesPage::open_note(const char *filename)
{
    snprintf(open_name_, sizeof(open_name_), "%s", filename);
    set_mode(Mode::Editor);
}

void NotesPage::save_note()
{
    if (textarea_ == nullptr) {
        return;
    }
    if (!services::storage_ready()) {
        notify("no SD card: cannot save");
        return;
    }
    services::storage_mkdir(kDir);

    if (open_name_[0] == '\0') {
        make_note_name(open_name_, sizeof(open_name_));
    }

    char path[256];
    services::storage_join(path, sizeof(path), kDir, open_name_);

    const char *text = lv_textarea_get_text(textarea_);
    const size_t len = (text != nullptr) ? strlen(text) : 0;

    const esp_err_t err = services::storage_write(path, text, len);
    if (err == ESP_OK) {
        dirty_ = false;
        ESP_LOGI(TAG, "saved %s (%u bytes)", path, (unsigned)len);
        notify("saved");
        if (note_label_ != nullptr) {
            lv_label_set_text(note_label_, open_name_);
        }
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        notify("save failed");
    }
}

void NotesPage::delete_note(size_t index)
{
    if (index >= entry_count_) {
        return;
    }
    char path[256];
    services::storage_join(path, sizeof(path), kDir, entries_[index].name);

    const esp_err_t err = services::storage_remove(path);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "deleted %s", path);
        load_list();
    } else {
        ESP_LOGE(TAG, "cannot delete %s: %s", path, esp_err_to_name(err));
        if (note_label_ != nullptr) {
            lv_label_set_text_fmt(note_label_, "delete failed: %s", esp_err_to_name(err));
        }
        armed_ = -1;
    }
}

void NotesPage::set_cn_strip(bool on)
{
    cn_on_ = on;
    if (cn_strip_ != nullptr) {
        if (on) {
            lv_obj_remove_flag(cn_strip_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(cn_strip_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void NotesPage::notify(const char *text)
{
    if (note_label_ == nullptr) {
        return;
    }
    /* In editor mode the footer label is the filename; a transient message
     * replaces it and the next save/close restores it.  Acceptable because the
     * filename is also shown in the header of the list this editor returns to. */
    lv_label_set_text(note_label_, text ? text : "");
}

/* ------------------------------------------------------------------------ */
/* events                                                                   */
/* ------------------------------------------------------------------------ */

void NotesPage::row_cb(lv_event_t *e)
{
    NotesPage *self = static_cast<NotesPage *>(lv_event_get_user_data(e));
    lv_obj_t *row = lv_event_get_target_obj(e);
    if (self == nullptr || row == nullptr) {
        return;
    }
    const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
    if (tag <= 0 || (size_t)tag > self->entry_count_) {
        return;
    }
    self->open_note(self->entries_[tag - 1].name);
}

void NotesPage::trash_cb(lv_event_t *e)
{
    NotesPage *self = static_cast<NotesPage *>(lv_event_get_user_data(e));
    lv_obj_t *bin = lv_event_get_target_obj(e);
    if (self == nullptr || bin == nullptr) {
        return;
    }
    const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(bin));
    if (tag <= 0) {
        return;
    }
    const int index = (int)tag - 1;

    /* Two-step delete, same rule as the file manager: tap to arm, tap to do. */
    if (self->armed_ != index) {
        self->armed_ = index;
        lv_obj_t *row = lv_obj_get_parent(bin);
        if (row != nullptr) {
            lv_obj_set_style_bg_color(row, lv_color_hex(Theme::kDanger), 0);
        }
        if (self->note_label_ != nullptr) {
            lv_label_set_text(self->note_label_, "tap the bin again to delete");
        }
        if (self->arm_timer_ != nullptr) {
            lv_timer_delete(self->arm_timer_);
        }
        self->arm_timer_ = lv_timer_create(arm_tick, kArmTimeoutMs, self);
        return;
    }

    self->delete_note((size_t)index);
}

void NotesPage::arm_tick(lv_timer_t *timer)
{
    NotesPage *self = static_cast<NotesPage *>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    self->armed_ = -1;
    self->load_list();   /* rebuilds the rows and clears the red tint */

    if (self->arm_timer_ != nullptr) {
        lv_timer_delete(self->arm_timer_);
        self->arm_timer_ = nullptr;
    }
}

void NotesPage::new_cb(lv_event_t *e)
{
    NotesPage *self = static_cast<NotesPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->open_name_[0] = '\0';
    self->set_mode(Mode::Editor);
}

void NotesPage::save_cb(lv_event_t *e)
{
    NotesPage *self = static_cast<NotesPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->save_note();
    }
}

void NotesPage::close_cb(lv_event_t *e)
{
    NotesPage *self = static_cast<NotesPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    /* Unsaved edits are announced rather than silently dropped, but the close
     * still goes ahead: on a device with no dialog, blocking the exit would be
     * worse than losing the text the user can see is unsaved. */
    if (self->dirty_) {
        ESP_LOGW(TAG, "closing with unsaved changes in '%s'",
                 self->open_name_[0] ? self->open_name_ : "(new note)");
    }
    self->open_name_[0] = '\0';
    self->set_mode(Mode::List);
}

void NotesPage::cn_toggle_cb(lv_event_t *e)
{
    NotesPage *self = static_cast<NotesPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->set_cn_strip(!self->cn_on_);
    }
}

void NotesPage::cn_char_cb(lv_event_t *e)
{
    NotesPage *self = static_cast<NotesPage *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (self == nullptr || self->textarea_ == nullptr || btn == nullptr) {
        return;
    }
    const char *ch = static_cast<const char *>(lv_obj_get_user_data(btn));
    if (ch == nullptr) {
        return;
    }
    lv_textarea_add_text(self->textarea_, ch);
    self->dirty_ = true;
}

void NotesPage::kb_cb(lv_event_t *e)
{
    NotesPage *self = static_cast<NotesPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        self->save_note();
        return;
    }
    self->dirty_ = true;
}

void NotesPage::back_cb(lv_event_t *)
{
    app::go_back();
}
