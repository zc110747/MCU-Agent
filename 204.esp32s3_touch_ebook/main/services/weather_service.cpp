#include "weather_service.h"

#include <stdio.h>
#include <string.h>

#include "clock_service.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "weather";

namespace services {

namespace {

constexpr uint32_t kRequestDelayMs = 1200;   /* models the network round trip */
constexpr uint32_t kTaskStack     = 4096;
constexpr UBaseType_t kTaskPrio   = 4;

WeatherState s_state = WeatherState::Idle;
WeatherData  s_data = {};
bool         s_busy = false;
bool         s_pending = false;
TaskHandle_t s_task = nullptr;

const char *const kConditions[] = {"Clear", "Partly cloudy", "Cloudy", "Light rain", "Overcast"};

/**
 * @brief Where a real reading would come from.
 *
 * Kept as the single seam so that wiring a live HTTP endpoint later touches
 * this function and nothing else.  It returns an error state instead of
 * inventing data whenever the source is unavailable.
 */
WeatherState fetch(WeatherData *out)
{
    if (out == nullptr) {
        return WeatherState::ApiError;
    }

    /* Stage 1: no WiFi credentials exist on the device, so there is nothing to
     * talk to.  The sample values below are generated locally and flagged as
     * such - they exist so the page's layout and all of its state handling can
     * be exercised on real hardware. */
    vTaskDelay(pdMS_TO_TICKS(kRequestDelayMs));

    TimeParts now;
    clock_now(&now);

    static uint32_t s_tick = 0;
    ++s_tick;

    *out = WeatherData{};
    out->is_sample = true;
    out->temp_c = 18.0f + (float)(s_tick % 7);
    out->humidity = 55 + (int)(s_tick % 30);
    out->wind_kmh = 6 + (int)(s_tick % 12);
    out->pressure_hpa = 1010 + (int)(s_tick % 12);
    snprintf(out->condition, sizeof(out->condition), "%s",
             kConditions[s_tick % (sizeof(kConditions) / sizeof(kConditions[0]))]);
    snprintf(out->city, sizeof(out->city), "Sample data");
    snprintf(out->observed, sizeof(out->observed), "%02d:%02d", now.hour, now.minute);

    return WeatherState::Ready;
}

void task(void *)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        s_busy = true;
        s_state = WeatherState::Loading;

        WeatherData fresh = {};
        const WeatherState result = fetch(&fresh);

        if (result == WeatherState::Ready) {
            s_data = fresh;
        }
        s_state = result;
        s_busy = false;

        ESP_LOGI(TAG, "refresh finished: %s", weather_state_text(result));
    }
}

}  // namespace

esp_err_t weather_init()
{
    if (s_task != nullptr) {
        return ESP_OK;
    }
    if (xTaskCreate(task, "weather", kTaskStack, nullptr, kTaskPrio, &s_task) != pdPASS) {
        // cppcheck-suppress knownConditionTrueFalse
        ESP_LOGE(TAG, "could not start the weather task");
        s_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "service up (stage 1: sample data)");
    return ESP_OK;
}

void weather_request_refresh()
{
    if (s_task == nullptr) {
        weather_init();
        if (s_task == nullptr) {
            return;
        }
    }
    /* Coalesce: a user tapping refresh ten times queues one request, not ten. */
    if (s_busy || s_pending) {
        return;
    }
    s_pending = true;
    s_state = WeatherState::Loading;
    xTaskNotifyGive(s_task);
}

WeatherState weather_state()
{
    return s_state;
}

const WeatherData *weather_data()
{
    return (s_state == WeatherState::Ready) ? &s_data : nullptr;
}

bool weather_busy()
{
    return s_state == WeatherState::Loading;
}

const char *weather_state_text(WeatherState st)
{
    switch (st) {
    case WeatherState::Idle:         return "idle";
    case WeatherState::Loading:      return "loading";
    case WeatherState::Ready:        return "ok";
    case WeatherState::NoNetwork:    return "no network";
    case WeatherState::Timeout:      return "timed out";
    case WeatherState::NetworkError: return "network error";
    case WeatherState::ApiError:     return "bad response";
    default:                         return "?";
    }
}

}  // namespace services
