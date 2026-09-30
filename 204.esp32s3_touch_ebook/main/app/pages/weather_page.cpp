/**
 * @file weather_page.cpp
 * @brief Phase 11: current conditions, fetched without blocking the UI.
 *
 * THE PART THE SPECIFICATION IS EMPHATIC ABOUT
 * --------------------------------------------
 * No network work happens in an event callback.  Tapping Refresh calls
 * weather_request_refresh(), which sets a flag and returns; a service-owned task
 * does the waiting.  This page then *polls* the service from a 500 ms LVGL
 * timer and repaints only when something actually changed.
 *
 * Polling rather than a callback is deliberate: a callback would arrive on the
 * service's task, and touching LVGL from a foreign task means taking the LVGL
 * mutex, which is exactly the deadlock the specification is warning about.
 *
 * Every failure state the service can report gets its own presentation, and the
 * page stays responsive throughout - the timer keeps running and the Back button
 * keeps working while a request is in flight.
 */

#include "weather_page.h"

#include <stdio.h>
#include <string.h>

#include "app_icons.h"
#include "app_manager.h"
#include "theme.h"
#include "widgets.h"

#include "esp_log.h"

static const char *TAG = "weather";

/* Twice the service's own redraw granularity: fast enough that a 1.2 s fetch
 * feels immediate, slow enough to be invisible in the CPU budget. */
static constexpr uint32_t kPollMs = 500;

void WeatherPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Weather", true);
    root_ = page.root;

    ui::icon_button(page.header_right, &icon_refresh, 44, refresh_cb, this);

    lv_obj_t *body = page.body;
    lv_obj_set_style_pad_row(body, Theme::kGapMd, 0);

    /* ---- the data layout ---------------------------------------------- */
    values_ = lv_obj_create(body);
    lv_obj_remove_style_all(values_);
    lv_obj_set_width(values_, LV_PCT(100));
    lv_obj_set_height(values_, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(values_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(values_, Theme::kGapMd, 0);
    lv_obj_clear_flag(values_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *now = ui::app_card(values_);
    lv_obj_set_flex_align(now, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    temp_label_ = lv_label_create(now);
    lv_obj_set_style_text_font(temp_label_, Theme::font_hero(), 0);
    lv_obj_set_style_text_color(temp_label_, lv_color_hex(Theme::kText), 0);
    lv_label_set_text(temp_label_, "--");

    cond_label_ = lv_label_create(now);
    lv_obj_add_style(cond_label_, Theme::text_title(), 0);
    lv_label_set_text(cond_label_, "--");

    lv_obj_t *stats = ui::app_card(values_, "Details");
    humidity_row_ = ui::info_row(stats, "Humidity", "--");
    wind_row_ = ui::info_row(stats, "Wind", "--");
    pressure_row_ = ui::info_row(stats, "Pressure", "--");
    observed_row_ = ui::info_row(stats, "Observed", "--");

    /* ---- footer -------------------------------------------------------- */
    footer_note_ = lv_label_create(page.footer_left);
    lv_obj_add_style(footer_note_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(footer_note_, Theme::font_small(), 0);
    lv_label_set_text(footer_note_, "");

    ESP_LOGI(TAG, "weather page built");
}

void WeatherPage::destroy()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    values_ = nullptr;
    message_ = nullptr;
    temp_label_ = nullptr;
    cond_label_ = nullptr;
    humidity_row_ = nullptr;
    wind_row_ = nullptr;
    pressure_row_ = nullptr;
    observed_row_ = nullptr;
    footer_note_ = nullptr;
}

void WeatherPage::on_enter()
{
    first_pass_ = true;
    last_state_ = services::WeatherState::Idle;
    last_observed_[0] = '\0';

    /* Ask for data and return.  Nothing here waits. */
    services::weather_request_refresh();
    apply_state(services::weather_state());

    timer_ = lv_timer_create(tick_cb, kPollMs, this);
}

void WeatherPage::on_leave()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

void WeatherPage::tick_cb(lv_timer_t *timer)
{
    WeatherPage *self = static_cast<WeatherPage *>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    self->apply_state(services::weather_state());
}

void WeatherPage::apply_state(services::WeatherState st)
{
    const services::WeatherData *d = services::weather_data();

    /* Nothing changed: do not touch a single label.  This is what keeps a page
     * that polls twice a second from costing anything when idle. */
    const char *observed = (d != nullptr) ? d->observed : "";
    const bool same_state = (st == last_state_) && !first_pass_;
    const bool same_data = (strncmp(observed, last_observed_, sizeof(last_observed_)) == 0);
    if (same_state && (st != services::WeatherState::Ready || same_data)) {
        return;
    }

    last_state_ = st;
    first_pass_ = false;
    snprintf(last_observed_, sizeof(last_observed_), "%s", observed);

    if (st == services::WeatherState::Ready && d != nullptr) {
        show_values(d);
    } else {
        show_message(st);
    }
}

void WeatherPage::show_values(const services::WeatherData *d)
{
    /* Tearing the message down here rather than in a shared helper keeps the
     * "only one of the two layouts exists" invariant in one place. */
    if (message_ != nullptr) {
        lv_obj_delete(message_);
        message_ = nullptr;
    }
    if (values_ != nullptr) {
        lv_obj_remove_flag(values_, LV_OBJ_FLAG_HIDDEN);
    }

    if (temp_label_ != nullptr) {
        /* One decimal place: a tenth of a degree is the resolution the sensor
         * actually has, and printing more would be inventing precision. */
        lv_label_set_text_fmt(temp_label_, "%.1f C", (double)d->temp_c);
    }
    if (cond_label_ != nullptr) {
        lv_label_set_text_fmt(cond_label_, "%s  -  %s", d->condition, d->city);
    }
    if (humidity_row_ != nullptr) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d %%", d->humidity);
        ui::info_row_set(humidity_row_, buf);
    }
    if (wind_row_ != nullptr) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d km/h", d->wind_kmh);
        ui::info_row_set(wind_row_, buf);
    }
    if (pressure_row_ != nullptr) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d hPa", d->pressure_hpa);
        ui::info_row_set(pressure_row_, buf);
    }
    if (observed_row_ != nullptr) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s", d->observed);
        ui::info_row_set(observed_row_, buf);
    }

    if (footer_note_ != nullptr) {
        /* A sample reading must never be mistaken for a real one, so the notice
         * is on screen whenever is_sample is set - not only in a log line. */
        lv_label_set_text(footer_note_,
                          d->is_sample ? "sample data - stage 1, no network request"
                                       : "live data");
    }

    ESP_LOGI(TAG, "shown: %.1f C, %s, %s", (double)d->temp_c, d->condition,
             d->is_sample ? "sample" : "live");
}

void WeatherPage::show_message(services::WeatherState st)
{
    if (values_ != nullptr) {
        lv_obj_add_flag(values_, LV_OBJ_FLAG_HIDDEN);
    }

    /* Rebuild rather than retitle: the empty state's icon and detail line
     * differ per state, and mutating three labels to match is more code than
     * dropping the box. */
    if (message_ != nullptr) {
        lv_obj_delete(message_);
    }

    const char *icon = "--";
    const char *title = "";
    const char *detail = "";

    switch (st) {
    case services::WeatherState::Loading:
        icon = "..";
        title = "Loading";
        detail = "The request runs on a service task, so the UI stays responsive.";
        break;
    case services::WeatherState::NoNetwork:
        icon = "!!";
        title = "No network";
        detail = "This device has no WiFi credentials yet. "
                 "Weather falls back to sample data once a network is configured.";
        break;
    case services::WeatherState::Timeout:
        icon = "!!";
        title = "Timed out";
        detail = "The service gave up waiting for the server. Tap refresh to retry.";
        break;
    case services::WeatherState::NetworkError:
        icon = "!!";
        title = "Network error";
        detail = "The request could not reach the server (DNS or connection).";
        break;
    case services::WeatherState::ApiError:
        icon = "!!";
        title = "Bad response";
        detail = "The server answered, but the payload was not usable.";
        break;
    default:
        icon = "--";
        title = "No data yet";
        detail = "Tap the refresh button to request a reading.";
        break;
    }

    message_ = ui::empty_state(root_, icon, title, detail);
    LV_UNUSED(message_);

    if (footer_note_ != nullptr) {
        lv_label_set_text_fmt(footer_note_, "state: %s", services::weather_state_text(st));
    }
}

void WeatherPage::refresh_cb(lv_event_t *)
{
    /* Returns immediately - see the file header. */
    services::weather_request_refresh();
}
