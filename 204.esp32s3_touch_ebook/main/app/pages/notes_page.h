/**
 * @file notes_page.h
 * @brief Phase 8: a plain-text scratchpad on the SD card.
 */
#pragma once

#include "lvgl.h"
#include "page.h"
#include "storage_service.h"

class NotesPage : public Page {
public:
    const char *name() const override { return "notes"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;
    void on_leave() override;

private:
    enum class Mode { List, Editor };

    static void row_cb(lv_event_t *e);
    static void trash_cb(lv_event_t *e);
    static void new_cb(lv_event_t *e);
    static void save_cb(lv_event_t *e);
    static void close_cb(lv_event_t *e);
    static void cn_toggle_cb(lv_event_t *e);
    static void cn_char_cb(lv_event_t *e);
    static void kb_cb(lv_event_t *e);
    static void arm_tick(lv_timer_t *t);

    void build_list();
    void build_editor();
    /** @brief Discard whatever mode is on screen and rebuild the shell. */
    void set_mode(Mode m);

    void load_list();
    void open_note(const char *filename);
    void save_note();
    void delete_note(size_t index);
    void set_cn_strip(bool on);

    /** @brief Where note files live; created on demand. */
    static constexpr const char *kDir = "/sd/notes";
    static constexpr size_t kMaxName = 96;

    /** @brief Flash a short line in the footer. */
    void notify(const char *text);

    Mode mode_ = Mode::List;

    lv_obj_t *body_ = nullptr;
    lv_obj_t *header_slot_ = nullptr;
    lv_obj_t *footer_left_ = nullptr;
    lv_obj_t *footer_right_ = nullptr;

    lv_obj_t *list_ = nullptr;
    lv_obj_t *textarea_ = nullptr;
    lv_obj_t *keyboard_ = nullptr;
    lv_obj_t *cn_strip_ = nullptr;
    lv_obj_t *note_label_ = nullptr;

    lv_timer_t *arm_timer_ = nullptr;
    int armed_ = -1;

    services::DirEntry entries_[services::kMaxEntriesPerPage] = {};
    size_t entry_count_ = 0;
    size_t listed_total_ = 0;

    /* Name of the file currently open in the editor, empty for a new note. */
    char open_name_[kMaxName] = {0};
    bool dirty_ = false;
    bool cn_on_ = false;
};
