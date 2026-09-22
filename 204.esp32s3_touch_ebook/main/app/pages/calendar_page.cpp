/**
 * @file calendar_page.cpp
 * @brief Phase 10: a month view with correct leap-year and month-length handling.
 *
 * The two things this page has to get right are the two things that are easy to
 * fake and obvious when faked: February in a leap year, and the weekday the
 * first of the month falls on.  Both come from clock_service (clock_days_in_month
 * and clock_day_of_week) rather than from arithmetic written here, so the
 * Calendar, the Clock and the header all agree by construction.
 *
 * The grid is always six rows.  A month that would need five rows simply has a
 * trailing blank week - which is what every paper calendar does, and the
 * alternative (a grid that changes height) makes the whole page jump every time
 * the month changes.
 */

#include "calendar_page.h"

#include <stdio.h>

#include "app_manager.h"
#include "clock_service.h"
#include "theme.h"
#include "widgets.h"

#include "esp_log.h"

static const char *TAG = "calendar";

namespace {

constexpr int kCols = 7;
constexpr int kRows = 6;
constexpr int kCells = kCols * kRows;

constexpr intptr_t kPrev = -1;
constexpr intptr_t kNext = 1;

/**
 * @brief Build a month-step button that knows its page and its direction.
 *
 * The page rides in the event's user data and the direction in the object's, so
 * the handler needs no global and no lookup of "which page is current".
 */
lv_obj_t *make_step_button(lv_obj_t *parent, const char *text, intptr_t delta, CalendarPage *page)
{
    lv_obj_t *btn = ui::app_button(parent, text, CalendarPage::step_cb, page);
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(delta));
    return btn;
}

}  // namespace

void CalendarPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Calendar", true);
    root_ = page.root;

    month_label_ = lv_label_create(page.header_right);
    lv_obj_add_style(month_label_, Theme::text_body(), 0);
    lv_label_set_text(month_label_, "----");

    lv_obj_t *body = page.body;
    lv_obj_set_style_pad_row(body, Theme::kGapSm, 0);

    /* ---- weekday header ------------------------------------------------ */
    lv_obj_t *weekdays = lv_obj_create(body);
    lv_obj_remove_style_all(weekdays);
    lv_obj_set_width(weekdays, LV_PCT(100));
    lv_obj_set_height(weekdays, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(weekdays, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(weekdays, LV_OBJ_FLAG_SCROLLABLE);

    static const int32_t wd_col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                     LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                     LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    /* A real one-row descriptor rather than NULL: the grid layout dereferences
     * the row array unconditionally, so "no rows" is not a thing it supports. */
    static const int32_t wd_row[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_layout(weekdays, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(weekdays, wd_col, wd_row);

    for (int i = 0; i < kCols; ++i) {
        lv_obj_t *label = lv_label_create(weekdays);
        lv_obj_add_style(label, Theme::text_dim(), 0);
        lv_obj_set_style_text_font(label, Theme::font_small(), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        /* Sunday first, matching clock_day_of_week()'s 0 = Sunday. */
        lv_label_set_text(label, services::clock_weekday_name(i));
        lv_obj_set_grid_cell(label, LV_GRID_ALIGN_STRETCH, i, 1,
                             LV_GRID_ALIGN_CENTER, 0, 1);
    }

    /* ---- the day grid -------------------------------------------------- */
    static const int32_t gcol[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t grow[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};

    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(grid, gcol, grow);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 2, 0);
    lv_obj_set_style_pad_column(grid, 2, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < kCells; ++i) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_style_radius(cell, Theme::kRadiusSm, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, i % kCols, 1,
                             LV_GRID_ALIGN_STRETCH, i / kCols, 1);

        lv_obj_t *label = lv_label_create(cell);
        lv_obj_set_style_text_font(label, Theme::font_body(), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(Theme::kText), 0);
        lv_label_set_text(label, "");
        lv_obj_center(label);

        cells_[i] = cell;
        cell_labels_[i] = label;
    }

    /* ---- detail -------------------------------------------------------- */
    lv_obj_t *detail = ui::app_card(body);
    select_row_ = ui::info_row(detail, "First day", "--");
    detail_row_ = ui::info_row(detail, "Length", "--");

    /* ---- footer -------------------------------------------------------- */
    make_step_button(page.footer_left, "< Prev", kPrev, this);
    ui::app_button(page.footer_left, "Today", today_cb, this);
    make_step_button(page.footer_left, "Next >", kNext, this);

    ui::app_button(page.footer_right, "Back", back_cb, nullptr);

    ESP_LOGI(TAG, "calendar built (%d cells, %d columns)", kCells, kCols);
}

void CalendarPage::destroy()
{
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    month_label_ = nullptr;
    detail_row_ = nullptr;
    select_row_ = nullptr;
    for (int i = 0; i < kCells; ++i) {
        cells_[i] = nullptr;
        cell_labels_[i] = nullptr;
    }
}

void CalendarPage::on_enter()
{
    services::TimeParts now;
    services::clock_now(&now);
    today_year_ = now.year;
    today_month_ = now.month;
    today_day_ = now.day;

    year_ = now.year;
    month_ = now.month;
    selected_day_ = now.day;

    render_month();
}

void CalendarPage::go_today()
{
    year_ = today_year_;
    month_ = today_month_;
    selected_day_ = today_day_;
    render_month();
}

void CalendarPage::shift_month(int delta)
{
    services::clock_add_months(&year_, &month_, delta);

    /* Keep the selected day legal: 31 January -> 31 February does not exist, so
     * it clamps to the last day of the new month instead of rolling over into
     * March, which is what a naive `month += 1` does. */
    const int len = services::clock_days_in_month(year_, month_);
    if (selected_day_ > len) {
        selected_day_ = len;
    }
    render_month();
}

void CalendarPage::render_month()
{
    const int len = services::clock_days_in_month(year_, month_);
    /* 0 = Sunday, matching the grid's first column. */
    const int first_wday = services::clock_day_of_week(year_, month_, 1);

    if (month_label_ != nullptr) {
        lv_label_set_text_fmt(month_label_, "%s %d",
                              services::clock_month_name(month_), year_);
    }

    for (int i = 0; i < kCells; ++i) {
        lv_obj_t *cell = cells_[i];
        lv_obj_t *label = cell_labels_[i];
        if (cell == nullptr || label == nullptr) {
            continue;
        }

        const int day = i - first_wday + 1;
        const bool in_month = (day >= 1 && day <= len);

        if (!in_month) {
            lv_label_set_text(label, "");
            lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            continue;
        }

        lv_label_set_text_fmt(label, "%d", day);

        const bool is_today = (year_ == today_year_ && month_ == today_month_ && day == today_day_);
        const bool is_selected = (day == selected_day_);

        if (is_selected) {
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(cell, lv_color_hex(Theme::kAccent), 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0x0E1013), 0);
            lv_obj_set_style_border_width(cell, 0, 0);
        } else if (is_today) {
            /* Today keeps the accent as an outline rather than a fill, so that
             * "today" and "selected" stay distinguishable when they differ. */
            lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(Theme::kAccent), 0);
            lv_obj_set_style_text_color(label, lv_color_hex(Theme::kText), 0);
        } else {
            lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(cell, 0, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(Theme::kTextDim), 0);
        }
    }

    render_detail();
}

void CalendarPage::render_detail()
{
    const int len = services::clock_days_in_month(year_, month_);
    const int wday = services::clock_day_of_week(year_, month_, 1);
    const int wday_sel = services::clock_day_of_week(year_, month_, selected_day_);

    if (select_row_ != nullptr) {
        ui::info_row_set(select_row_, services::clock_weekday_name(wday));
    }
    if (detail_row_ != nullptr) {
        /* Showing the leap-year verdict next to the length is what turns
         * "February has 29 days" from a claim into a checkable statement. */
        char buf[64];
        if (month_ == 2) {
            snprintf(buf, sizeof(buf), "%d days  (%s leap year)", len,
                     services::clock_is_leap_year(year_) ? "a" : "not a");
        } else {
            snprintf(buf, sizeof(buf), "%d days", len);
        }
        ui::info_row_set(detail_row_, buf);
    }

    ESP_LOGD(TAG, "%04d-%02d-%02d -> %s, %d days, first day %s",
             year_, month_, selected_day_, services::clock_month_name(month_),
             len, services::clock_weekday_name(wday_sel));
}

void CalendarPage::step_cb(lv_event_t *e)
{
    CalendarPage *self = static_cast<CalendarPage *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (self == nullptr || btn == nullptr) {
        return;
    }
    const intptr_t delta = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));
    self->shift_month((int)delta);
}

void CalendarPage::today_cb(lv_event_t *e)
{
    CalendarPage *self = static_cast<CalendarPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->go_today();
    }
}

void CalendarPage::back_cb(lv_event_t *)
{
    app::go_back();
}
