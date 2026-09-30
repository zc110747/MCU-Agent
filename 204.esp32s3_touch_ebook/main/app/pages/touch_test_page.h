/**
 * @file touch_test_page.h
 * @brief Phase 3 acceptance page: does a finger land where it is drawn?
 */
#pragma once

#include "lvgl.h"
#include "page.h"

class TouchTestPage : public Page {
public:
    const char *name() const override { return "touch_test"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;

private:
    static void pad_cb(lv_event_t *e);

    void move_marker(int32_t local_x, int32_t local_y);
    void mark_cell(int32_t local_x, int32_t local_y);
    void refresh_counts();

    lv_obj_t *pad_ = nullptr;
    lv_obj_t *marker_ = nullptr;
    lv_obj_t *coords_ = nullptr;
    lv_obj_t *cells_[9] = {};
    lv_obj_t *counts_ = nullptr;
    lv_obj_t *gesture_ = nullptr;

    lv_point_t press_origin_ = {};
    uint32_t taps_ = 0;
    uint32_t longs_ = 0;
    uint32_t swipes_ = 0;
};
