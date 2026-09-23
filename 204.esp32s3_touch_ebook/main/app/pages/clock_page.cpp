/**
 * @file clock_page.cpp
 * @brief Phase 10: a live clock face with 12/24 hour switching.
 *
 * The digits are 48 px because on this page the time *is* the content; a
 * caption-sized clock would leave 300 px of nothing under it.  The refresh
 * timer runs at 200 ms and redraws only when the second actually changes, so a
 * clock that shows seconds costs one label update per second rather than five.
 */

#include "clock_page.h"

#include <stdio.h>
#include <string.h>

#include "app_manager.h"
#include "clock_service.h"
#include "theme.h"
#include "widgets.h"

#include "esp_log.h"

static const char *TAG = "clock";

/* Faster than 1 Hz so the displayed second never lags the RTC by a visible
 * amount, but the redraw is gated on the second changing. */
static constexpr uint32_t kTickMs = 200;

namespace {

/* Adjustment codes.  They ride on the button's *object* user data rather than
 * the event's, because the event's slot already carries the page: a callback
 * that has to guess which page it belongs to would break the moment a second
 * page reused the same handler. */
constexpr intptr_t kHourMinus = -1;
constexpr intptr_t kHourPlus  = 1;
constexpr intptr_t kMinMinus  = -2;
constexpr intptr_t kMinPlus   = 2;

/**
 * @brief Build one adjustment button that knows both its page and its delta.
 */
lv_obj_t *make_adjust_button(lv_obj_t *parent, const char *text, intptr_t delta, ClockPage *page)
{
    lv_obj_t *btn = ui::app_button(parent, text, ClockPage::adjust_cb, page);
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(delta));
    return btn;
}

}  // namespace

void ClockPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Clock", true);
    root_ = page.root;

    format_btn_ = ui::app_button(page.header_right, "24H", format_cb, this);

    lv_obj_t *body = page.body;
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

    /* ---- the face ----------------------------------------------------- */
    lv_obj_t *face = ui::app_card(body);
    lv_obj_set_flex_grow(face, 1);
    lv_obj_set_flex_align(face, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(face, Theme::kGapSm, 0);

    time_label_ = lv_label_create(face);
    lv_obj_set_style_text_font(time_label_, Theme::font_hero(), 0);
    lv_obj_set_style_text_color(time_label_, lv_color_hex(Theme::kText), 0);
    lv_label_set_text(time_label_, "--:--:--");

    date_label_ = lv_label_create(face);
    lv_obj_set_style_text_font(date_label_, Theme::font_title(), 0);
    lv_obj_set_style_text_color(date_label_, lv_color_hex(Theme::kTextDim), 0);
    lv_label_set_text(date_label_, "---- -- --");

    /* ---- where the time comes from ------------------------------------ */
    lv_obj_t *info = ui::app_card(body, "Source");
    state_row_ = ui::info_row(info, "RTC", "--");
    source_row_ = ui::info_row(info, "Reference", "--");

    /* ---- setting it --------------------------------------------------- */
    lv_obj_t *set = ui::app_card(body, "Set time");
    lv_obj_t *note = lv_label_create(set);
    lv_obj_add_style(note, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(note, Theme::font_small(), 0);
    lv_label_set_text(note, "Adjustments are written straight to the PCF85063A, "
                            "so the change survives a power cycle.");
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *row = lv_obj_create(set);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, Theme::kGapSm, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    make_adjust_button(row, "Hour -", kHourMinus, this);
    make_adjust_button(row, "Hour +", kHourPlus, this);
    make_adjust_button(row, "Min -", kMinMinus, this);
    make_adjust_button(row, "Min +", kMinPlus, this);

    ESP_LOGI(TAG, "clock built");
}

void ClockPage::destroy()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    time_label_ = nullptr;
    date_label_ = nullptr;
    format_btn_ = nullptr;
    source_row_ = nullptr;
    state_row_ = nullptr;
}

void ClockPage::on_enter()
{
    last_second_ = -1;   /* force the first refresh to actually write */
    refresh();
    timer_ = lv_timer_create(tick_cb, kTickMs, this);
}

void ClockPage::on_leave()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void ClockPage::refresh()
{
    services::TimeParts now;
    services::clock_now(&now);

    if (now.second == last_second_) {
        return;   /* nothing on screen changes within the same second */
    }
    last_second_ = now.second;

    char time_buf[32];
    services::clock_format_time(time_buf, sizeof(time_buf), &now, true, h24_);
    lv_label_set_text(time_label_, time_buf);

    char date_buf[32];
    services::clock_format_date(date_buf, sizeof(date_buf), &now);
    lv_label_set_text_fmt(date_label_, "%s  %s", date_buf,
                          services::clock_weekday_name(now.weekday));

    if (state_row_ != nullptr) {
        ui::info_row_set(state_row_,
                         services::clock_valid() ? "PCF85063A, running"
                                                 : "PCF85063A, time not set");
    }
    if (source_row_ != nullptr) {
        /* Distinguishing "the RTC is keeping time" from "we are showing the
         * build timestamp" is the difference between a clock and a lie. */
        ui::info_row_set(source_row_,
                         services::clock_valid() ? "real time" : "build timestamp (approximate)");
    }
}

void ClockPage::tick_cb(lv_timer_t *timer)
{
    ClockPage *self = static_cast<ClockPage *>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->refresh();
    }
}

void ClockPage::format_cb(lv_event_t *e)
{
    ClockPage *self = static_cast<ClockPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->h24_ = !self->h24_;
    if (self->format_btn_ != nullptr) {
        lv_obj_t *label = lv_obj_get_child(self->format_btn_, 0);
        if (label != nullptr) {
            lv_label_set_text(label, self->h24_ ? "24H" : "12H");
        }
    }
    self->last_second_ = -1;   /* the 12/24 choice changes the text layout */
    self->refresh();
}

void ClockPage::adjust_cb(lv_event_t *e)
{
    ClockPage *self = static_cast<ClockPage *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (self == nullptr || btn == nullptr) {
        return;
    }

    const intptr_t what = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));

    services::TimeParts t;
    services::clock_now(&t);

    switch (what) {
    case kHourMinus: t.hour = (t.hour + 23) % 24; break;
    case kHourPlus:  t.hour = (t.hour + 1) % 24;  break;
    case kMinMinus:
        /* Stepping the minute backwards past :00 must roll the hour back too,
         * or the clock would jump an hour forward at the top of the hour. */
        if (--t.minute < 0) {
            t.minute = 59;
            t.hour = (t.hour + 23) % 24;
        }
        break;
    case kMinPlus:
        if (++t.minute > 59) {
            t.minute = 0;
            t.hour = (t.hour + 1) % 24;
        }
        break;
    default:
        return;
    }

    /* Seconds are zeroed so that the write is a defined instant rather than
     * "wherever the free-running counter happened to be". */
    t.second = 0;
    const esp_err_t err = services::clock_set(&t);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot set the RTC: %s", esp_err_to_name(err));
    }

    self->last_second_ = -1;
    self->refresh();
}
