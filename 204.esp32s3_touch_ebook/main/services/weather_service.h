/**
 * @file weather_service.h
 * @brief Weather data for the Weather page.
 *
 * THREADING CONTRACT (the part the spec is emphatic about)
 * -------------------------------------------------------
 * A page's event callback must never wait on the network.  weather_request_refresh()
 * therefore only sets a flag and returns; a service-owned task does the talking.
 * The page reads weather_state()/weather_data() from its LVGL timer and redraws
 * only when the state changes, so the UI keeps running through every failure
 * mode below.
 *
 * DATA SOURCE
 * -----------
 * Phase 11 stage 1 ships sample data.  The request path is fully modelled
 * (including the failure states) but plugged to a local generator rather than
 * an HTTP endpoint, because there are no WiFi credentials on this device yet.
 * weather_data()->is_sample says so explicitly, and the page prints it, so a
 * sample reading can never be mistaken for a real one.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

namespace services {

enum class WeatherState {
    Idle,          /* nothing requested yet                  */
    Loading,       /* request in flight                      */
    Ready,         /* weather_data() is valid                */
    NoNetwork,     /* no WiFi configured or connected        */
    Timeout,       /* the request took too long              */
    NetworkError,  /* DNS/connect failed                     */
    ApiError,      /* answered, but the payload was unusable */
};

struct WeatherData {
    bool  is_sample;
    float temp_c;
    int   humidity;      /* percent    */
    int   wind_kmh;
    int   pressure_hpa;
    char  condition[24]; /* "Cloudy"   */
    char  city[32];
    char  observed[24];  /* "13:45"    */
};

/** @brief Start the service task. Safe to call once, from app start-up. */
esp_err_t weather_init();

/** @brief Kick off a refresh. Returns immediately; never blocks. */
void weather_request_refresh();

/** @brief Current state. Cheap: one enum read. */
WeatherState weather_state();

/** @brief Valid when weather_state() == Ready, else NULL. */
const WeatherData *weather_data();

/** @brief Short human-readable state, for the page's status line. */
const char *weather_state_text(WeatherState st);

/** @brief True while a request is in flight. */
bool weather_busy();

}  // namespace services
