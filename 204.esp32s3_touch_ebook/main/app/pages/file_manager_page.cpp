/**
 * @file file_manager_page.cpp
 * @brief Phase 6: browse the SD card, open files, delete them.
 *
 * Three decisions worth stating
 * -----------------------------
 * 1. Deleting is two-step.  The first tap on a row's bin arms it and turns the
 *    row red; the second tap deletes.  Arming expires after a few seconds and is
 *    exclusive, so a single stray tap - the classic failure of touch UIs - can
 *    never destroy a file.  There is no modal dialog because a modal on a
 *    800x480 panel with no cursor is worse than an inline confirmation.
 *
 * 2. "Open" previews in place rather than navigating.  Handing a filename to
 *    another page would mean pages could receive arguments, which nothing else
 *    in this application needs; an in-place preview keeps every page's
 *    constructor argument-free and still satisfies "open".
 *
 * 3. The preview uses LVGL's own filesystem layer, so it exercises the same
 *    decoder path a real image viewer would.  That makes it a genuine test of
 *    the LVGL file-system integration rather than a second implementation.
 */

#include "file_manager_page.h"

#include <stdio.h>
#include <string.h>

#include "app_icons.h"
#include "app_manager.h"
#include "lvgl_fs.h"
#include "text_service.h"
#include "theme.h"
#include "widgets.h"

#include "esp_log.h"

static const char *TAG = "files";

namespace {

constexpr uint32_t kArmTimeoutMs = 4000;
constexpr const char *kRoot = "/sd";

/** @brief Human name for an entry's kind, used as the row subtitle. */
void describe_entry(const services::DirEntry &e, char *dst, size_t len)
{
    if (e.is_dir) {
        snprintf(dst, len, "directory");
        return;
    }
    char size[24];
    services::storage_human_size(size, sizeof(size), e.size);

    char ext[8];
    services::storage_extension(ext, sizeof(ext), e.name);
    if (ext[0] != '\0') {
        snprintf(dst, len, "%s  .%s", size, ext);
    } else {
        snprintf(dst, len, "%s", size);
    }
}

/** @brief Pick a leading glyph.  Falls back to the generic file icon. */
const lv_image_dsc_t *icon_for(const services::DirEntry &e)
{
    if (e.is_dir) {
        return &icon_files;
    }
    if (services::storage_is_image(e.name)) {
        return &icon_photos;
    }
    if (services::storage_is_text(e.name)) {
        return &icon_notes;
    }
    return &icon_files;
}

}  // namespace

/* ------------------------------------------------------------------------ */
/* build                                                                    */
/* ------------------------------------------------------------------------ */

void FileManagerPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Files", true);
    root_ = page.root;

    lv_obj_t *body = page.body;
    lv_obj_set_style_pad_row(body, Theme::kGapSm, 0);

    /* The body holds exactly one thing: the scrolling list.  The preview is a
     * sibling of the page root rather than a child here, so that it can overlay
     * the header and footer too. */
    list_ = lv_obj_create(body);
    lv_obj_remove_style_all(list_);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, Theme::kGapSm, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);

    path_label_ = lv_label_create(page.header_right);
    lv_obj_add_style(path_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(path_label_, Theme::font_small(), 0);
    lv_label_set_text(path_label_, kRoot);

    count_label_ = lv_label_create(page.footer_left);
    lv_obj_add_style(count_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(count_label_, Theme::font_small(), 0);
    lv_label_set_text(count_label_, "--");

    ui::app_button(page.footer_left, "Up", up_cb, this);

    ESP_LOGI(TAG, "file manager built");
}

void FileManagerPage::destroy()
{
    if (arm_timer_ != nullptr) {
        lv_timer_delete(arm_timer_);
        arm_timer_ = nullptr;
    }
    /* The preview lives under root_, so deleting root_ takes it with it. */
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    list_ = nullptr;
    preview_ = nullptr;
    path_label_ = nullptr;
    count_label_ = nullptr;
    armed_ = -1;
}

void FileManagerPage::on_enter()
{
    snprintf(dir_, sizeof(dir_), "%s", kRoot);
    load_dir(dir_);
}

void FileManagerPage::on_leave()
{
    if (arm_timer_ != nullptr) {
        lv_timer_delete(arm_timer_);
        arm_timer_ = nullptr;
    }
    armed_ = -1;
}

/* ------------------------------------------------------------------------ */
/* listing                                                                  */
/* ------------------------------------------------------------------------ */

void FileManagerPage::load_dir(const char *dir_path)
{
    if (list_ == nullptr) {
        return;
    }

    lv_obj_clean(list_);
    armed_ = -1;

    snprintf(dir_, sizeof(dir_), "%s", dir_path);

    /* storage_list() sorts directories first and then case-insensitively by
     * name, so the list is rendered exactly as it arrives. */
    const esp_err_t err = services::storage_list(dir_, entries_,
                                                 services::kMaxEntriesPerPage,
                                                 &listed_total_);
    entry_count_ = 0;

    if (err != ESP_OK) {
        entry_count_ = 0;
    } else {
        entry_count_ = listed_total_;
        if (entry_count_ > services::kMaxEntriesPerPage) {
            /* Capped rather than truncated silently: the footer says how many
             * exist, so a cut-off listing is visible instead of looking like a
             * short directory. */
            entry_count_ = services::kMaxEntriesPerPage;
        }
    }

    if (err != ESP_OK) {
        const char *title = (err == ESP_ERR_NOT_FOUND) ? "No card / no such folder"
                                                       : "Cannot read this folder";
        ui::empty_state(list_, "--", title,
                        services::storage_ready()
                            ? "The card is mounted but this path could not be listed."
                            : "No SD card is mounted. Insert one and press Up, then re-open Files.");
        update_footer();
        return;
    }

    for (size_t i = 0; i < entry_count_; ++i) {
        const services::DirEntry &e = entries_[i];

        char subtitle[40];
        describe_entry(e, subtitle, sizeof(subtitle));

        lv_obj_t *row = ui::list_item(list_, icon_for(e), e.name, subtitle,
                                      e.is_dir, row_cb, this);
        /* The index rides on the object, the page on the event - see the note
         * in clock_page.cpp for why they are two different slots. */
        lv_obj_set_user_data(row, reinterpret_cast<void *>(i + 1));

        if (!e.is_dir) {
            /* A trailing bin on file rows only; a directory needs to be emptied
             * before it can go, and pretending otherwise would be a lie. */
            lv_obj_t *bin = ui::icon_button(row, &icon_trash, 40, trash_cb, this);
            lv_obj_set_user_data(bin, reinterpret_cast<void *>(i + 1));
        }
    }

    if (entry_count_ == 0) {
        ui::empty_state(list_, "--", "This folder is empty",
                        "Copy files onto the card, or go Up to another folder.");
    }

    if (path_label_ != nullptr) {
        lv_label_set_text(path_label_, dir_);
    }
    update_footer();
    ESP_LOGI(TAG, "%s: %u entries listed", dir_, (unsigned)entry_count_);
}

void FileManagerPage::update_footer()
{
    if (count_label_ == nullptr) {
        return;
    }
    if (!services::storage_ready()) {
        lv_label_set_text(count_label_, "no SD card");
        return;
    }
    if (listed_total_ > entry_count_) {
        lv_label_set_text_fmt(count_label_, "%u of %u entries",
                              (unsigned)entry_count_, (unsigned)listed_total_);
    } else {
        lv_label_set_text_fmt(count_label_, "%u entries", (unsigned)entry_count_);
    }
}

/* ------------------------------------------------------------------------ */
/* opening                                                                  */
/* ------------------------------------------------------------------------ */

void FileManagerPage::open_entry(size_t index)
{
    if (index >= entry_count_) {
        return;
    }
    const services::DirEntry &e = entries_[index];

    char path[256];
    services::storage_join(path, sizeof(path), dir_, e.name);

    /* Entering a directory replaces the listing in place; there is no separate
     * history stack, because "Up" walks back out and that is all the
     * specification asks for. */
    if (e.is_dir) {
        load_dir(path);
        return;
    }

    close_preview();

    /* The overlay is a child of the page root, laid out by nothing.  Without
     * IGNORE_LAYOUT the root's flex column would position and possibly resize
     * it, and a full-page overlay cannot be a flex item. */
    preview_ = lv_obj_create(root_);
    lv_obj_remove_style_all(preview_);
    lv_obj_add_flag(preview_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(preview_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(preview_, 0, 0);
    lv_obj_clear_flag(preview_, LV_OBJ_FLAG_SCROLLABLE);

    /* with_close = false: this is an in-page overlay, not a pushed page.  Its
     * own "Close" below dismisses the overlay, which is the action that makes
     * sense here; the header's exit button would jump straight to Home and
     * skip the file list the user is still working in. */
    ui::PageLayout pv = ui::page_layout(preview_, e.name, true, false);

    char info[64];
    describe_entry(e, info, sizeof(info));
    lv_obj_t *sub = lv_label_create(pv.header_right);
    lv_obj_add_style(sub, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(sub, Theme::font_small(), 0);
    lv_label_set_text(sub, info);

    ui::app_button(pv.footer_right, "Close", close_preview_cb, this);

    if (services::storage_is_image(e.name)) {
        /* Contain, not stretch: an image whose aspect ratio differs from the
         * panel's is letterboxed, which is the requirement and is also what
         * LVGL's own align mode does.  Nothing here computes a scale factor. */
        lv_obj_t *img = lv_image_create(pv.body);
        lv_obj_set_size(img, LV_PCT(100), LV_PCT(100));
        lv_image_set_inner_align(img, LV_IMAGE_ALIGN_CONTAIN);

        char lv_path[264];
        if (ui::lvgl_path(lv_path, sizeof(lv_path), path)) {
            lv_image_set_src(img, lv_path);
        }
        ESP_LOGI(TAG, "preview image %s", path);
        return;
    }

    if (services::storage_is_text(e.name)) {
        services::TextDoc doc;
        const esp_err_t err = services::text_load_file(path, &doc);
        if (err != ESP_OK) {
            char detail[128];
            snprintf(detail, sizeof(detail), "%s (%s)", esp_err_to_name(err),
                     services::text_encoding_name(doc.encoding));
            ui::empty_state(pv.body, "--", "Cannot show this file", detail);
            services::text_free(&doc);
            return;
        }

        char desc[64];
        services::text_describe(&doc, desc, sizeof(desc));
        lv_obj_t *meta = lv_label_create(pv.header_right);
        lv_obj_add_style(meta, Theme::text_dim(), 0);
        lv_obj_set_style_text_font(meta, Theme::font_small(), 0);
        lv_label_set_text(meta, desc);

        lv_obj_t *text = lv_label_create(pv.body);
        /* The CJK font is used for every preview rather than only for files
         * that look Chinese: it covers Latin too, and switching fonts based on
         * a guess would make the page's appearance depend on a heuristic. */
        lv_obj_set_style_text_font(text, Theme::font_cjk(), 0);
        lv_obj_set_style_text_color(text, lv_color_hex(Theme::kText), 0);
        lv_obj_set_width(text, LV_PCT(100));
        lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_WRAP);
        lv_label_set_text(text, doc.utf8);

        services::text_free(&doc);
        return;
    }

    ui::empty_state(pv.body, "--", "No viewer for this file",
                    "This build previews .txt, .md, .log, .jpg, .jpeg, .png and .bmp.");
}

void FileManagerPage::close_preview()
{
    if (preview_ != nullptr) {
        lv_obj_delete(preview_);
        preview_ = nullptr;
    }
}

/* ------------------------------------------------------------------------ */
/* deleting                                                                 */
/* ------------------------------------------------------------------------ */

void FileManagerPage::delete_entry(size_t index)
{
    if (index >= entry_count_) {
        return;
    }
    char path[256];
    services::storage_join(path, sizeof(path), dir_, entries_[index].name);

    const esp_err_t err = services::storage_remove(path);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "deleted %s", path);
        load_dir(dir_);   /* reload so the row actually goes away */
    } else {
        ESP_LOGE(TAG, "cannot delete %s: %s", path, esp_err_to_name(err));
        /* Say why in place: rebuilding the list would wipe the message, so the
         * footer carries it. */
        if (count_label_ != nullptr) {
            lv_label_set_text_fmt(count_label_, "delete failed: %s", esp_err_to_name(err));
        }
        armed_ = -1;
    }
}

/* ------------------------------------------------------------------------ */
/* events                                                                   */
/* ------------------------------------------------------------------------ */

void FileManagerPage::row_cb(lv_event_t *e)
{
    FileManagerPage *self = static_cast<FileManagerPage *>(lv_event_get_user_data(e));
    lv_obj_t *row = lv_event_get_target_obj(e);
    if (self == nullptr || row == nullptr) {
        return;
    }
    const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
    if (tag <= 0) {
        return;
    }
    self->open_entry((size_t)tag - 1);
}

void FileManagerPage::trash_cb(lv_event_t *e)
{
    FileManagerPage *self = static_cast<FileManagerPage *>(lv_event_get_user_data(e));
    lv_obj_t *bin = lv_event_get_target_obj(e);
    if (self == nullptr || bin == nullptr) {
        return;
    }
    const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(bin));
    if (tag <= 0) {
        return;
    }
    const int index = (int)tag - 1;

    /* Arm on the first tap, act on the second. */
    if (self->armed_ != index) {
        if (self->armed_ >= 0 && (size_t)self->armed_ < self->entry_count_) {
            lv_obj_t *prev_row = lv_obj_get_child(self->list_, self->armed_);
            if (prev_row != nullptr) {
                lv_obj_set_style_bg_color(prev_row, lv_color_hex(Theme::kSurface2), 0);
            }
        }
        self->armed_ = index;

        /* The row is the button's parent; recolouring it is what makes the
         * armed state visible, and it is the only feedback available without a
         * dialog. */
        lv_obj_t *row = lv_obj_get_parent(bin);
        if (row != nullptr) {
            lv_obj_set_style_bg_color(row, lv_color_hex(Theme::kDanger), 0);
        }
        if (self->count_label_ != nullptr) {
            lv_label_set_text(self->count_label_, "tap the bin again to delete");
        }
        if (self->arm_timer_ != nullptr) {
            lv_timer_delete(self->arm_timer_);
        }
        self->arm_timer_ = lv_timer_create(arm_tick, kArmTimeoutMs, self);
        return;
    }

    self->delete_entry((size_t)index);
}

void FileManagerPage::arm_tick(lv_timer_t *timer)
{
    FileManagerPage *self = static_cast<FileManagerPage *>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    /* Expire the arming and put the row's colour back. */
    if (self->armed_ >= 0 && self->list_ != nullptr &&
        (size_t)self->armed_ < self->entry_count_) {
        lv_obj_t *row = lv_obj_get_child(self->list_, self->armed_);
        if (row != nullptr) {
            lv_obj_set_style_bg_color(row, lv_color_hex(Theme::kSurface2), 0);
        }
    }
    self->armed_ = -1;
    self->update_footer();

    if (self->arm_timer_ != nullptr) {
        lv_timer_delete(self->arm_timer_);
        self->arm_timer_ = nullptr;
    }
}

void FileManagerPage::up_cb(lv_event_t *e)
{
    FileManagerPage *self = static_cast<FileManagerPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    /* Already at the mount point: reloading it is the useful action, because it
     * is also how a card inserted after boot gets picked up. */
    if (strcmp(self->dir_, kRoot) == 0) {
        services::storage_init();
        self->load_dir(kRoot);
        return;
    }

    char parent[192];
    snprintf(parent, sizeof(parent), "%s", self->dir_);
    char *slash = strrchr(parent, '/');
    if (slash != nullptr && slash != parent) {
        *slash = '\0';
    } else {
        snprintf(parent, sizeof(parent), "%s", kRoot);
    }
    /* Never walk above the mount point: there is nothing there to list. */
    if (strncmp(parent, kRoot, strlen(kRoot)) != 0) {
        snprintf(parent, sizeof(parent), "%s", kRoot);
    }
    self->load_dir(parent);
}

void FileManagerPage::close_preview_cb(lv_event_t *e)
{
    FileManagerPage *self = static_cast<FileManagerPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->close_preview();
    }
}
