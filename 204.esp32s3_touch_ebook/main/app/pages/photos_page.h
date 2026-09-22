/**
 * @file photos_page.h
 * @brief Phase 7: browse and view JPEG / PNG / BMP files from the card.
 */
#pragma once

#include "lvgl.h"
#include "page.h"
#include "storage_service.h"

class PhotosPage : public Page {
public:
    const char *name() const override { return "photos"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;

private:
    static void row_cb(lv_event_t *e);
    static void prev_cb(lv_event_t *e);
    static void next_cb(lv_event_t *e);
    static void back_cb(lv_event_t *e);

    void load_list();

    /** @brief Show image @p index of the current listing (wraps at both ends). */
    void show(size_t index);

    /** @brief Direction is +1 or -1; wraps so Next on the last image loops. */
    void step(int direction);

    void update_caption();

    lv_obj_t *list_ = nullptr;
    lv_obj_t *preview_ = nullptr;
    lv_obj_t *image_ = nullptr;
    lv_obj_t *caption_ = nullptr;
    lv_obj_t *status_label_ = nullptr;

    services::DirEntry entries_[services::kMaxEntriesPerPage] = {};
    size_t entry_count_ = 0;
    size_t listed_total_ = 0;
    int    current_ = -1;

    /** Directory actually used, so the LVGL path can be rebuilt. */
    char dir_[96] = {0};

    static constexpr const char *kPhotoDir = "/sd/photos";
    static constexpr const char *kFallbackDir = "/sd";
};
