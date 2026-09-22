/**
 * @file calendar_page.h
 * @brief Phase 10: a month view with correct leap-year and month-length handling.
 */
#pragma once

#include "lvgl.h"
#include "page.h"

class CalendarPage : public Page {
public:
    const char *name() const override { return "calendar"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;

    /** @brief Month step handler; carries the page so the factory can be a free function. */
    static void step_cb(lv_event_t *e);

private:
    static void today_cb(lv_event_t *e);
    static void back_cb(lv_event_t *e);

    /** @brief Roll the shown month and redraw. */
    void shift_month(int delta);
    void go_today();

    /** @brief Repaint the 42 day cells and the header for the current month. */
    void render_month();
    void render_detail();

    lv_obj_t *month_label_ = nullptr;
    lv_obj_t *cells_[42] = {};       /* lv_obj_t of each cell                  */
    lv_obj_t *cell_labels_[42] = {}; /* the day number inside each cell        */
    lv_obj_t *detail_row_ = nullptr;
    lv_obj_t *select_row_ = nullptr;

    int year_ = 2026;
    int month_ = 1;
    int selected_day_ = 1;
    int today_year_ = 0, today_month_ = 0, today_day_ = 0;
};
