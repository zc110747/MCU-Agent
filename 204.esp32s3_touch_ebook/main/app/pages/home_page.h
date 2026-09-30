/**
 * @file home_page.h
 * @brief The root page: a grid of application tiles.
 */
#pragma once

#include "lvgl.h"
#include "page.h"

class HomePage : public Page {
public:
    const char *name() const override { return "home"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;
    void on_leave() override;

    /* Event handlers are public because the tile factory lives in an
     * anonymous namespace in the .cpp; a free function cannot reach private
     * class members, and hiding a static event handler buys nothing. */
    static void tile_cb(lv_event_t *e);

private:
    static void diag_cb(lv_event_t *e);
    static void status_tick(lv_timer_t *t);

    /** @brief Refresh the header readout. Safe to call outside a timer. */
    void refresh_status();

    lv_obj_t *status_label_ = nullptr;
    lv_timer_t *status_timer_ = nullptr;
};
