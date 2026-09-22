/**
 * @file drawing_page.h
 * @brief Phase 9: a touch canvas that can be cleared and saved as a BMP.
 */
#pragma once

#include "lvgl.h"
#include "page.h"

class DrawingPage : public Page {
public:
    const char *name() const override { return "drawing"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;

    /** @brief Canvas input handler; public so the static factory below can reach it. */
    static void canvas_cb(lv_event_t *e);

    /** @brief Pen colour / width selection. Public for the same reason. */
    static void pen_cb(lv_event_t *e);

private:
    static void clear_cb(lv_event_t *e);
    static void save_cb(lv_event_t *e);
    static void back_cb(lv_event_t *e);

    /** @brief Paint one segment into the canvas. */
    void stroke(int32_t x0, int32_t y0, int32_t x1, int32_t y1);

    /** @brief Fill the canvas with the background colour. */
    void clear();

    /** @brief Write the canvas to /sd/notes/... as a 24-bit BMP. */
    void save();

    lv_obj_t   *canvas_ = nullptr;
    void       *pixels_ = nullptr;
    int32_t     canvas_w_ = 0;
    int32_t     canvas_h_ = 0;

    lv_point_t  last_ = {};
    bool        have_last_ = false;

    lv_color_t  pen_color_ = {};
    int32_t     pen_width_ = 6;

    lv_obj_t   *footer_note_ = nullptr;
    lv_obj_t   *swatches_[5] = {};
    lv_obj_t   *widths_[3] = {};
};
