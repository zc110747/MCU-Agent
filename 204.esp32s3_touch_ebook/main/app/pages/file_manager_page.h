/**
 * @file file_manager_page.h
 * @brief Phase 6: browse the SD card, open files, delete them.
 */
#pragma once

#include "lvgl.h"
#include "page.h"
#include "storage_service.h"

class FileManagerPage : public Page {
public:
    const char *name() const override { return "file_manager"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;
    void on_leave() override;

private:
    static void row_cb(lv_event_t *e);
    static void trash_cb(lv_event_t *e);
    static void up_cb(lv_event_t *e);
    static void back_cb(lv_event_t *e);
    static void close_preview_cb(lv_event_t *e);
    static void arm_tick(lv_timer_t *t);

    /** @brief Populate @p dir_path into the list, replacing whatever was there. */
    void load_dir(const char *dir_path);

    /** @brief Open (preview) the file at index @p i of the current listing. */
    void open_entry(size_t i);

    /** @brief Delete the file at index @p i, after it has been armed. */
    void delete_entry(size_t i);

    void close_preview();
    void update_footer();

    lv_obj_t *list_ = nullptr;
    lv_obj_t *preview_ = nullptr;
    lv_obj_t *path_label_ = nullptr;
    lv_obj_t *count_label_ = nullptr;
    lv_timer_t *arm_timer_ = nullptr;

    char  dir_[192] = {0};
    services::DirEntry entries_[services::kMaxEntriesPerPage] = {};
    size_t entry_count_ = 0;
    size_t listed_total_ = 0;

    /* Two-step delete: the first tap arms the row, the second one deletes it.
     * Arming expires after a few seconds, and arming a different row disarms
     * the previous one - so a stray tap can never destroy a file. */
    int armed_ = -1;
};
