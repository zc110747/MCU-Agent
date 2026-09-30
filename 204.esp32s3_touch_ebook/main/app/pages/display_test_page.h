/**
 * @file display_test_page.h
 * @brief Phase 1 acceptance screen: prove the RGB panel really works.
 *
 * What it answers, in one glance at the panel:
 *   colour     - 8 bar test pattern + 5 colour chips (R/B mis-wiring shows up
 *                immediately as swapped chips)
 *   geometry   - a 24x24 square inside the test image and an 80x80 circle;
 *                if either is an ellipse / rectangle, something is scaling
 *   resolution - a 1 px divider line must be exactly 1 px, and the header
 *                reads back the panel geometry from board_config.h
 *   refresh    - the footer counts seconds, which only moves if the LVGL task
 *                is scheduling and the frame really reaches the glass
 */
#pragma once

#include "page.h"
#include "lvgl.h"

class DisplayTestPage : public Page {
public:
    const char *name() const override { return "display_test"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;
    void on_leave() override;

private:
    lv_obj_t *make_card(lv_obj_t *parent, const char *title);
    lv_obj_t *make_chip(lv_obj_t *parent, uint32_t rgb, const char *text, uint32_t text_rgb);
    lv_obj_t *make_shape_card(lv_obj_t *parent);
    lv_obj_t *make_image_card(lv_obj_t *parent);

    static void uptime_cb(lv_timer_t *timer);

    lv_obj_t *uptime_label_ = nullptr;
    lv_timer_t *uptime_timer_ = nullptr;
    uint32_t seconds_ = 0;
};
