/**
 * @file weather_page.h
 * @brief Phase 11: current conditions, fetched without blocking the UI.
 */
#pragma once

#include "lvgl.h"
#include "page.h"
#include "weather_service.h"

class WeatherPage : public Page {
public:
    const char *name() const override { return "weather"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;
    void on_leave() override;

private:
    static void tick_cb(lv_timer_t *t);
    static void refresh_cb(lv_event_t *e);

    /** @brief Apply whatever the service currently says. */
    void apply_state(services::WeatherState st);

    void show_values(const services::WeatherData *d);
    void show_message(services::WeatherState st);

    lv_obj_t *values_ = nullptr;    /* the data layout, hidden on failure */
    lv_obj_t *message_ = nullptr;   /* the empty/error layout              */
    lv_obj_t *temp_label_ = nullptr;
    lv_obj_t *cond_label_ = nullptr;
    lv_obj_t *humidity_row_ = nullptr;
    lv_obj_t *wind_row_ = nullptr;
    lv_obj_t *pressure_row_ = nullptr;
    lv_obj_t *observed_row_ = nullptr;
    lv_obj_t *footer_note_ = nullptr;

    lv_timer_t *timer_ = nullptr;
    services::WeatherState last_state_ = services::WeatherState::Idle;
    /**
     * The observation stamp of the data currently on screen.
     *
     * Two consecutive successful refreshes both leave the state at Ready, so
     * comparing the state alone would silently skip the second update and the
     * page would look stuck.  The stamp changes with every fetch, so it is what
     * actually detects "new data arrived".
     */
    char last_observed_[24] = {0};
    bool first_pass_ = true;
};
