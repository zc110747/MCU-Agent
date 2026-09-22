/**
 * @file clock_page.h
 * @brief Phase 10: a live clock face with 12/24 hour switching.
 */
#pragma once

#include "lvgl.h"
#include "page.h"

class ClockPage : public Page {
public:
    const char *name() const override { return "clock"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;
    void on_leave() override;

    /**
     * @brief Hour/minute nudge handler.
     *
     * Public because the button factory lives in an anonymous namespace in the
     * .cpp and a free function cannot reach a private static member; hiding it
     * would buy nothing and force the factory to be a member instead.
     */
    static void adjust_cb(lv_event_t *e);

private:
    static void tick_cb(lv_timer_t *t);
    static void format_cb(lv_event_t *e);
    static void back_cb(lv_event_t *e);

    void refresh();

    lv_obj_t *time_label_ = nullptr;
    lv_obj_t *date_label_ = nullptr;
    lv_obj_t *format_btn_ = nullptr;
    lv_obj_t *source_row_ = nullptr;
    lv_obj_t *state_row_ = nullptr;
    lv_timer_t *timer_ = nullptr;

    bool h24_ = true;
    int last_second_ = -1;
};
