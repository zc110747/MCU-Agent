/**
 * @file photos_page.cpp
 * @brief Phase 7: browse and view JPEG / PNG / BMP files from the card.
 *
 * ASPECT RATIO
 * ------------
 * The specification is explicit: a 1600x900 image shown on an 800x480 panel must
 * become 800x450 with 30 px of letterbox - never 800x480.  That is implemented
 * by LVGL's LV_IMAGE_ALIGN_CONTAIN, which is the library's own fit-within
 * calculation.  No scale factor is computed here, and in particular there is no
 * `scale_x = 800 / image_width` anywhere: that expression is the defect the
 * specification names, and hand-writing it would be doing it even if the result
 * happened to look right for one image.
 *
 * WHY THE LAYOUT IS A LIST PLUS ONE PREVIEW
 * -----------------------------------------
 * A grid of thumbnails reads better, and it was the first design.  It was
 * dropped after reading the decoder: LVGL's JPEG path decodes at the image's
 * full resolution, so a contact sheet of six 1600x1200 photos would hold
 * ~23 MB of decoded pixels.  With 8 MB of PSRAM total - most of it already
 * committed to the two panel frame buffers - that is not a "probably fine".
 * One preview at a time keeps the peak equal to the largest single image, which
 * is a number this page can actually log and defend.  The memory line in the
 * caption exists so that number is visible on the device rather than assumed.
 */

#include "photos_page.h"

#include <stdio.h>
#include <string.h>

#include "app_icons.h"
#include "app_manager.h"
#include "lvgl_fs.h"
#include "system_info.h"
#include "theme.h"
#include "widgets.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "photos";

void PhotosPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Photos", true);
    root_ = page.root;

    lv_obj_t *body = page.body;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, Theme::kGapMd, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

    /* ---- the file list ------------------------------------------------ */
    list_ = lv_obj_create(body);
    lv_obj_remove_style_all(list_);
    lv_obj_set_height(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 2);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, Theme::kGapSm, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);

    /* ---- the preview -------------------------------------------------- */
    preview_ = lv_obj_create(body);
    lv_obj_remove_style_all(preview_);
    lv_obj_add_style(preview_, Theme::card(), 0);
    lv_obj_set_height(preview_, LV_PCT(100));
    lv_obj_set_flex_grow(preview_, 3);
    lv_obj_set_style_pad_all(preview_, 0, 0);
    lv_obj_clear_flag(preview_, LV_OBJ_FLAG_SCROLLABLE);

    /* The image fills the preview box and CONTAIN letterboxes it inside.  This
     * is the whole of the aspect-ratio handling. */
    image_ = lv_image_create(preview_);
    lv_obj_set_size(image_, LV_PCT(100), LV_PCT(100));
    lv_image_set_inner_align(image_, LV_IMAGE_ALIGN_CONTAIN);

    /* ---- chrome -------------------------------------------------------- */
    caption_ = lv_label_create(page.footer_left);
    lv_obj_add_style(caption_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(caption_, Theme::font_small(), 0);
    lv_label_set_text(caption_, "no image selected");

    status_label_ = lv_label_create(page.header_right);
    lv_obj_add_style(status_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(status_label_, Theme::font_small(), 0);
    lv_label_set_text(status_label_, "--");

    ui::app_button(page.footer_right, "Prev", prev_cb, this);
    ui::app_button(page.footer_right, "Next", next_cb, this);

    ESP_LOGI(TAG, "photos built (list + single preview, contain fit)");
}

void PhotosPage::destroy()
{
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    list_ = nullptr;
    preview_ = nullptr;
    image_ = nullptr;
    caption_ = nullptr;
    status_label_ = nullptr;
    current_ = -1;
}

void PhotosPage::on_enter()
{
    load_list();
}

void PhotosPage::load_list()
{
    if (list_ == nullptr) {
        return;
    }
    lv_obj_clean(list_);
    entry_count_ = 0;
    listed_total_ = 0;
    current_ = -1;

    if (!services::storage_ready()) {
        ui::empty_state(list_, "--", "No SD card",
                        "Images are read from " "/sd/photos" " (or /sd). "
                        "Insert a card and reopen this page.");
        if (caption_ != nullptr) {
            lv_label_set_text(caption_, "no card");
        }
        return;
    }

    const char *dirs[2] = {kPhotoDir, kFallbackDir};
    esp_err_t err = ESP_ERR_NOT_FOUND;
    for (const char *dir : dirs) {
        err = services::storage_list(dir, entries_, services::kMaxEntriesPerPage,
                                    &listed_total_);
        if (err == ESP_OK) {
            snprintf(dir_, sizeof(dir_), "%s", dir);
            break;
        }
    }

    if (err != ESP_OK) {
        ui::empty_state(list_, "--", "Cannot read the card",
                        "Neither /sd/photos nor /sd could be listed.");
        return;
    }

    size_t shown = 0;
    for (size_t i = 0; i < listed_total_ && i < services::kMaxEntriesPerPage; ++i) {
        const services::DirEntry &e = entries_[i];
        if (e.is_dir || !services::storage_is_image(e.name)) {
            continue;
        }

        char size[24];
        services::storage_human_size(size, sizeof(size), e.size);

        lv_obj_t *row = ui::list_item(list_, &icon_photos, e.name, size,
                                      false, row_cb, this);
        lv_obj_set_user_data(row, reinterpret_cast<void *>(i + 1));
        ++shown;
    }
    entry_count_ = shown;

    if (shown == 0) {
        ui::empty_state(list_, "--", "No images found",
                        "Put .jpg, .jpeg, .png or .bmp files in /sd/photos.");
        if (status_label_ != nullptr) {
            lv_label_set_text(status_label_, "0 images");
        }
        return;
    }

    if (status_label_ != nullptr) {
        lv_label_set_text_fmt(status_label_, "%u image%s", (unsigned)shown,
                              shown == 1 ? "" : "s");
    }
    ESP_LOGI(TAG, "listed %u images from %s", (unsigned)shown, dir_);

    /* Show the first one straight away: an empty preview pane next to a full
     * list looks like the page failed. */
    show(0);
}

void PhotosPage::show(size_t index)
{
    if (entry_count_ == 0 || image_ == nullptr) {
        return;
    }
    if (index >= entry_count_) {
        index = 0;
    }

    /* The listing stored indices are 1-based tags; only image rows are counted
     * in entry_count_, so walk the array and pick the index-th image. */
    size_t seen = 0;
    const services::DirEntry *chosen = nullptr;
    size_t chosen_slot = 0;
    for (size_t i = 0; i < listed_total_ && i < services::kMaxEntriesPerPage; ++i) {
        const services::DirEntry &e = entries_[i];
        if (e.is_dir || !services::storage_is_image(e.name)) {
            continue;
        }
        if (seen == index) {
            chosen = &e;
            chosen_slot = i;
            break;
        }
        ++seen;
    }
    if (chosen == nullptr) {
        return;
    }

    char path[256];
    services::storage_join(path, sizeof(path), dir_, chosen->name);

    char lv_path[264];
    if (!ui::lvgl_path(lv_path, sizeof(lv_path), path)) {
        ESP_LOGE(TAG, "path too long: %s", path);
        return;
    }

    /* Setting the source is what triggers the decode; LVGL caches the result
     * and the previous image is dropped when it falls out of the cache. */
    lv_image_set_src(image_, lv_path);
    current_ = (int)index;
    (void)chosen_slot;

    /* Repaint the selected row so the list and the preview agree about which
     * image is on screen. */
    if (list_ != nullptr) {
        const uint32_t rows = lv_obj_get_child_count(list_);
        for (uint32_t r = 0; r < rows; ++r) {
            lv_obj_t *row = lv_obj_get_child(list_, (int32_t)r);
            const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
            const bool on = (tag > 0 && (size_t)tag - 1 == chosen_slot);
            lv_obj_set_style_bg_color(row,
                                      lv_color_hex(on ? Theme::kSurface2 : Theme::kSurface), 0);
            lv_obj_set_style_border_color(row,
                                          lv_color_hex(on ? Theme::kAccent : Theme::kBorder), 0);
        }
    }

    update_caption();
    ESP_LOGI(TAG, "[%d/%u] %s", (int)index + 1, (unsigned)entry_count_, path);
}

void PhotosPage::step(int direction)
{
    if (entry_count_ == 0) {
        return;
    }
    /* Wrapping rather than clamping: on a photo viewer, Next at the end
     * returning to the first is what a user expects, and it removes a dead
     * state where the button looks broken. */
    int next = current_ + direction;
    if (next < 0) {
        next = (int)entry_count_ - 1;
    } else if (next >= (int)entry_count_) {
        next = 0;
    }
    show((size_t)next);
}

void PhotosPage::update_caption()
{
    if (caption_ == nullptr || current_ < 0) {
        return;
    }

    char size[24];
    if ((size_t)current_ < entry_count_) {
        /* Find the file size of the shown image by walking the same way show()
         * does - the listing is a filtered view, so the indices differ. */
        size_t seen = 0;
        for (size_t i = 0; i < listed_total_ && i < services::kMaxEntriesPerPage; ++i) {
            const services::DirEntry &e = entries_[i];
            if (e.is_dir || !services::storage_is_image(e.name)) {
                continue;
            }
            if (seen == (size_t)current_) {
                services::storage_human_size(size, sizeof(size), e.size);
                break;
            }
            ++seen;
        }
    } else {
        snprintf(size, sizeof(size), "?");
    }

    /* The free-heap figure is on screen on purpose: this page is the one whose
     * cost depends on the image, so the number that proves it stayed in budget
     * belongs next to the image. */
    system_info_t info;
    system_info_collect(&info);
    lv_label_set_text_fmt(caption_, "%d/%u   %s   PSRAM %uK free",
                          current_ + 1, (unsigned)entry_count_, size,
                          (unsigned)(info.heap_psram_free / 1024));
}

/* ------------------------------------------------------------------------ */
/* events                                                                   */
/* ------------------------------------------------------------------------ */

void PhotosPage::row_cb(lv_event_t *e)
{
    PhotosPage *self = static_cast<PhotosPage *>(lv_event_get_user_data(e));
    lv_obj_t *row = lv_event_get_target_obj(e);
    if (self == nullptr || row == nullptr) {
        return;
    }
    const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(row));
    if (tag <= 0) {
        return;
    }

    /* Translate the array slot into an image ordinal. */
    const size_t slot = (size_t)tag - 1;
    size_t ordinal = 0;
    for (size_t i = 0; i < slot && i < services::kMaxEntriesPerPage; ++i) {
        const services::DirEntry &e2 = self->entries_[i];
        if (!e2.is_dir && services::storage_is_image(e2.name)) {
            ++ordinal;
        }
    }
    self->show(ordinal);
}

void PhotosPage::prev_cb(lv_event_t *e)
{
    PhotosPage *self = static_cast<PhotosPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->step(-1);
    }
}

void PhotosPage::next_cb(lv_event_t *e)
{
    PhotosPage *self = static_cast<PhotosPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->step(1);
    }
}
