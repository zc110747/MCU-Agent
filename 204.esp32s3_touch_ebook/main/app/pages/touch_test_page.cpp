/**
 * @file touch_test_page.cpp
 * @brief Phase 3 acceptance page.
 *
 * What this page has to make obvious
 * ----------------------------------
 * The task spec asks for four corners, the centre, buttons, swipes and long
 * presses to be verifiable, and for "finger at A, ink at B" to be impossible.
 * So the page is built around one large pad:
 *
 *   - a marker follows the finger exactly.  It is drawn from the *same*
 *     coordinates LVGL would use to place a widget, so if the marker tracks the
 *     finger then any control on any page will land under the finger too;
 *   - the pad is divided into a 3x3 grid and each cell lights up once it has
 *     been touched, which turns "did you hit all four corners and the middle"
 *     into a count of nine lit cells rather than an act of memory;
 *   - a centre crosshair gives a fixed reference to aim at, so a constant
 *     offset (the classic wrong calibration) is visible immediately;
 *   - taps, long presses and swipe directions are counted separately.
 */

#include "touch_test_page.h"

#include "app_manager.h"
#include "theme.h"
#include "widgets.h"

#include "esp_log.h"

static const char *TAG = "touch_test";

/* A press that moves further than this between down and up counts as a swipe
 * rather than a tap.  Matches LVGL's own scrolling threshold in spirit. */
static constexpr int32_t kSwipeMinPx = 40;

namespace {

const char *swipe_name(int32_t dx, int32_t dy)
{
    if (dx > 0 && (dx >= (dy < 0 ? -dy : dy))) return "right";
    if (dx < 0 && (-dx >= (dy < 0 ? -dy : dy))) return "left";
    if (dy < 0) return "up";
    if (dy > 0) return "down";
    return "-";
}

}  // namespace

void TouchTestPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Touch Test", true);
    root_ = page.root;

    /* ---- header right: how to read this page --------------------------- */
    lv_obj_t *hint = lv_label_create(page.header_right);
    lv_obj_add_style(hint, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(hint, Theme::font_small(), 0);
    lv_label_set_text(hint, "drag inside the pad");

    /* ---- body: pad on the left, instrumentation on the right ---------- */
    lv_obj_t *body = page.body;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(body, Theme::kGapLg, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

    /* --- the pad ------------------------------------------------------- */
    pad_ = lv_obj_create(body);
    lv_obj_remove_style_all(pad_);
    lv_obj_add_style(pad_, Theme::card(), 0);
    lv_obj_set_height(pad_, LV_PCT(100));
    lv_obj_set_flex_grow(pad_, 3);
    lv_obj_add_flag(pad_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(pad_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(pad_, 0, 0);

    /* Centre crosshair: a fixed aiming reference. */
    lv_obj_t *cross_h = lv_obj_create(pad_);
    lv_obj_remove_style_all(cross_h);
    lv_obj_set_size(cross_h, 40, 2);
    lv_obj_set_style_bg_color(cross_h, lv_color_hex(Theme::kBorder), 0);
    lv_obj_set_style_bg_opa(cross_h, LV_OPA_COVER, 0);
    lv_obj_center(cross_h);

    lv_obj_t *cross_v = lv_obj_create(pad_);
    lv_obj_remove_style_all(cross_v);
    lv_obj_set_size(cross_v, 2, 40);
    lv_obj_set_style_bg_color(cross_v, lv_color_hex(Theme::kBorder), 0);
    lv_obj_set_style_bg_opa(cross_v, LV_OPA_COVER, 0);
    lv_obj_center(cross_v);

    /* The marker is NOT clickable, so it never steals the press from the pad;
     * events keep flowing to pad_cb(). */
    marker_ = lv_obj_create(pad_);
    lv_obj_remove_style_all(marker_);
    lv_obj_set_size(marker_, 22, 22);
    lv_obj_set_style_radius(marker_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(marker_, lv_color_hex(Theme::kAccent), 0);
    lv_obj_set_style_bg_opa(marker_, LV_OPA_70, 0);
    lv_obj_clear_flag(marker_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(marker_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(marker_, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_event_cb(pad_, pad_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(pad_, pad_cb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(pad_, pad_cb, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(pad_, pad_cb, LV_EVENT_LONG_PRESSED, this);

    /* --- instrumentation column ---------------------------------------- */
    lv_obj_t *side = lv_obj_create(body);
    lv_obj_remove_style_all(side);
    lv_obj_set_height(side, LV_PCT(100));
    lv_obj_set_flex_grow(side, 2);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, Theme::kGapMd, 0);
    lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *grid_title = lv_label_create(side);
    lv_obj_add_style(grid_title, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(grid_title, Theme::font_small(), 0);
    lv_label_set_text(grid_title, "COVERAGE - touch all nine cells");

    static const int32_t gcol[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    static const int32_t grow[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    lv_obj_t *grid = lv_obj_create(side);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, gcol, grow);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, Theme::kGapSm, 0);
    lv_obj_set_style_pad_column(grid, Theme::kGapSm, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 9; ++i) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_style_radius(cell, Theme::kRadiusSm, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(Theme::kSurface2), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(Theme::kBorder), 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, i % 3, 1,
                             LV_GRID_ALIGN_STRETCH, i / 3, 1);
        cells_[i] = cell;
    }

    counts_ = lv_label_create(side);
    lv_obj_add_style(counts_, Theme::text_body(), 0);
    lv_label_set_text(counts_, "taps 0   long 0");

    gesture_ = lv_label_create(side);
    lv_obj_add_style(gesture_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(gesture_, Theme::font_small(), 0);
    lv_label_set_text(gesture_, "swipes 0   last -");

    /* ---- footer ------------------------------------------------------- */
    coords_ = lv_label_create(page.footer_left);
    lv_obj_add_style(coords_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(coords_, Theme::font_small(), 0);
    lv_label_set_text(coords_, "x = ---   y = ---");

    ESP_LOGI(TAG, "touch test page built");
}

void TouchTestPage::destroy()
{
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    pad_ = nullptr;
    marker_ = nullptr;
    coords_ = nullptr;
    counts_ = nullptr;
    gesture_ = nullptr;
    for (int i = 0; i < 9; ++i) {
        cells_[i] = nullptr;
    }
}

void TouchTestPage::move_marker(int32_t local_x, int32_t local_y)
{
    if (marker_ == nullptr) {
        return;
    }
    lv_obj_remove_flag(marker_, LV_OBJ_FLAG_HIDDEN);
    /* Aligned from the pad's centre rather than positioned absolutely, so the
     * marker follows the pad if the flex layout ever resizes it.            */
    const lv_coord_t w = lv_obj_get_width(pad_);
    const lv_coord_t h = lv_obj_get_height(pad_);
    lv_obj_align(marker_, LV_ALIGN_CENTER, local_x - w / 2, local_y - h / 2);
}

void TouchTestPage::mark_cell(int32_t local_x, int32_t local_y)
{
    const lv_coord_t w = lv_obj_get_width(pad_);
    const lv_coord_t h = lv_obj_get_height(pad_);
    if (w <= 0 || h <= 0) {
        return;
    }

    int32_t col = (local_x * 3) / w;
    int32_t row = (local_y * 3) / h;
    if (col < 0) col = 0;
    if (col > 2) col = 2;
    if (row < 0) row = 0;
    if (row > 2) row = 2;

    lv_obj_t *cell = cells_[row * 3 + col];
    if (cell != nullptr) {
        lv_obj_set_style_bg_color(cell, lv_color_hex(Theme::kAccent), 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(Theme::kAccent), 0);
    }
}

void TouchTestPage::refresh_counts()
{
    if (counts_ != nullptr) {
        lv_label_set_text_fmt(counts_, "taps %u   long %u",
                              (unsigned)taps_, (unsigned)longs_);
    }
}

void TouchTestPage::pad_cb(lv_event_t *e)
{
    TouchTestPage *self = static_cast<TouchTestPage *>(lv_event_get_user_data(e));
    lv_indev_t *indev = lv_event_get_indev(e);
    if (self == nullptr || self->pad_ == nullptr) {
        return;
    }
    /* A synthetic event (e.g. lv_obj_send_event from the code) has no indev;
     * lv_indev_get_point() would then dereference NULL. */
    if (indev == nullptr) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    lv_area_t pad_area;
    lv_obj_get_coords(self->pad_, &pad_area);
    const int32_t lx = point.x - pad_area.x1;
    const int32_t ly = point.y - pad_area.y1;

    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        self->press_origin_ = point;
        ++self->taps_;
        self->refresh_counts();
        self->move_marker(lx, ly);
        self->mark_cell(lx, ly);
        if (self->coords_ != nullptr) {
            lv_label_set_text_fmt(self->coords_, "x = %d   y = %d", (int)point.x, (int)point.y);
        }
        break;

    case LV_EVENT_PRESSING:
        /* Continuous: this is what makes a calibration offset obvious, because
         * the marker must stay exactly under the finger while it moves.     */
        self->move_marker(lx, ly);
        self->mark_cell(lx, ly);
        if (self->coords_ != nullptr) {
            lv_label_set_text_fmt(self->coords_, "x = %d   y = %d", (int)point.x, (int)point.y);
        }
        break;

    case LV_EVENT_LONG_PRESSED:
        ++self->longs_;
        self->refresh_counts();
        if (self->gesture_ != nullptr) {
            lv_label_set_text_fmt(self->gesture_, "swipes %u   last long press",
                                  (unsigned)self->swipes_);
        }
        break;

    case LV_EVENT_RELEASED: {
        const int32_t dx = point.x - self->press_origin_.x;
        const int32_t dy = point.y - self->press_origin_.y;
        const int32_t adx = dx < 0 ? -dx : dx;
        const int32_t ady = dy < 0 ? -dy : dy;
        if (adx > kSwipeMinPx || ady > kSwipeMinPx) {
            ++self->swipes_;
            if (self->gesture_ != nullptr) {
                lv_label_set_text_fmt(self->gesture_, "swipes %u   last %s (%d,%d)",
                                      (unsigned)self->swipes_, swipe_name(dx, dy),
                                      (int)dx, (int)dy);
            }
        }
        break;
    }

    default:
        break;
    }
}
