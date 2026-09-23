/**
 * @file reader_page.h
 * @brief Phase 5: open a TXT file from the card and read it page by page.
 */
#pragma once

#include "lvgl.h"
#include "page.h"
#include "storage_service.h"
#include "text_service.h"

class ReaderPage : public Page {
public:
    const char *name() const override { return "reader"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;

private:
    enum class Mode { List, Reading };

    static void row_cb(lv_event_t *e);
    static void back_cb(lv_event_t *e);
    static void list_cb(lv_event_t *e);
    static void prev_cb(lv_event_t *e);
    static void next_cb(lv_event_t *e);
    static void font_cb(lv_event_t *e);

    void build_list();
    void build_reading();

    void load_list();
    void open_book(const char *filename);
    void close_book();

    /** @brief Apply the selected font and re-lay out (scroll resets to the top). */
    void apply_font();

    /** @brief Repaint the progress / metadata footer. */
    void refresh_progress();

    /** @brief Scroll exactly one viewport, in @p direction (-1 up, +1 down). */
    void turn_page(int direction);

    lv_obj_t *body_ = nullptr;
    lv_obj_t *header_slot_ = nullptr;
    lv_obj_t *footer_left_ = nullptr;
    lv_obj_t *footer_right_ = nullptr;

    lv_obj_t *list_ = nullptr;
    lv_obj_t *viewport_ = nullptr;
    lv_obj_t *text_ = nullptr;     /* the label inside the viewport */
    lv_obj_t *font_btn_ = nullptr;
    lv_obj_t *meta_label_ = nullptr;
    lv_obj_t *progress_label_ = nullptr;

    services::DirEntry entries_[services::kMaxEntriesPerPage] = {};
    size_t entry_count_ = 0;
    size_t listed_total_ = 0;

    services::TextDoc doc_;
    char  open_name_[96] = {0};
    int   font_index_ = 0;
    Mode  mode_ = Mode::List;

    /* Where a book is looked for first.  The task specification names this
     * directory, so it is tried before the card root. */
    static constexpr const char *kBookDir = "/sd/Ebook/txt";
    /* A card prepared by hand keeps its books next to the other content
     * folders rather than in a "txt" subfolder, so that level is searched in
     * between the two the task specification names. */
    static constexpr const char *kEbookDir = "/sd/Ebook";
    static constexpr const char *kFallbackDir = "/sd";
};
