/**
 * @file app_manager.cpp
 * @brief Page registry, navigation and the application task.
 */

#include "app_manager.h"

#include <new>

#include "display_test_page.h"
#include "lvgl_port.h"
#include "system_info.h"
#include "theme.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app";

namespace app {

/* Memory heartbeat: the acceptance run is 30 min+, so print a snapshot often
 * enough to see a leak growing but rarely enough not to spam the log. */
static constexpr uint32_t kMemLogPeriodMs = 30000;

/**
 * @brief Count the whole widget tree below @p obj (LVGL 9 has no such helper).
 *
 * This is the leak detector for the "Home -> Reader -> Home must release the
 * Reader widgets" requirement: after popping back to Home the number must
 * equal the number measured right after the first Home was built.
 */
static uint32_t count_objects(const lv_obj_t *obj)
{
    if (obj == NULL) {
        return 0;
    }
    uint32_t total = 1;
    const uint32_t children = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < children; ++i) {
        total += count_objects(lv_obj_get_child(obj, (int32_t)i));
    }
    return total;
}

AppManager &AppManager::instance()
{
    static AppManager s_instance;
    return s_instance;
}

esp_err_t AppManager::init()
{
    if (screen_ != NULL) {
        return ESP_OK;
    }
    if (lvgl_port_display() == NULL) {
        ESP_LOGE(TAG, "no LVGL display: lvgl_port_start() must run first");
        return ESP_ERR_INVALID_STATE;
    }

    Theme::init();

    screen_ = lv_screen_active();
    lv_obj_remove_style_all(screen_);
    lv_obj_add_style(screen_, Theme::screen(), 0);
    /* lv_obj_clear_flag() takes exactly one flag, so this is two calls - the
     * OR'd form only compiles in C, not C++. */
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLL_ELASTIC);

    if (depth_ == 0) {
        push(PageId::DisplayTest);
    }

    ESP_LOGI(TAG, "app ready: page '%s', lvgl objects = %u",
             current_ ? current_->name() : "-", (unsigned)count_objects(screen_));
    return ESP_OK;
}

void AppManager::request_restart()
{
    restart_req_ = true;
}

void AppManager::run()
{
    /* The UI is driven by the LVGL task created by esp_lvgl_port; this task
     * only exists to own the application lifetime and to report health. */
    uint32_t elapsed_ms = 0;

    while (!restart_req_) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed_ms += 100;
        if (elapsed_ms >= kMemLogPeriodMs) {
            elapsed_ms = 0;
            system_info_log_memory(TAG);

            /* Walking the widget tree touches LVGL, so take the lock - this
             * task runs outside the LVGL task by construction. */
            lvgl_port_acquire(0);
            if (current_ != NULL) {
                ESP_LOGI(TAG, "page '%s'  lvgl objects = %u",
                         current_->name(), (unsigned)count_objects(screen_));
            }
            lvgl_port_release();
        }
    }

    ESP_LOGW(TAG, "restart requested");
}

void AppManager::push(PageId id)
{
    if (depth_ >= kMaxDepth) {
        ESP_LOGW(TAG, "navigation stack full (%d), ignoring push", kMaxDepth);
        return;
    }
    show(id);
    history_[depth_++] = id;
}

void AppManager::pop()
{
    if (depth_ <= 1) {
        return;
    }
    --depth_;
    show(history_[depth_ - 1]);
}

void AppManager::show(PageId id)
{
    /* --- tear the outgoing page down completely ----------------------- */
    if (current_ != NULL) {
        const char *leaving = current_->name();
        current_->on_leave();
        current_->destroy();
        delete current_;
        current_ = NULL;
        ESP_LOGD(TAG, "page '%s' destroyed, lvgl objects = %u",
                 leaving, (unsigned)count_objects(screen_));
    }

    /* --- build the incoming one --------------------------------------- */
    current_ = instantiate(id);
    if (current_ == NULL) {
        ESP_LOGE(TAG, "no page registered for id %d", (int)id);
        return;
    }

    current_->create(screen_);
    if (current_->root() == NULL) {
        ESP_LOGE(TAG, "page '%s' failed to create its root object", current_->name());
        delete current_;
        current_ = NULL;
        return;
    }

    current_->on_enter();
    ESP_LOGI(TAG, "page '%s' entered, lvgl objects = %u",
             current_->name(), (unsigned)count_objects(screen_));
}

Page *AppManager::instantiate(PageId id) const
{
    switch (id) {
    case PageId::DisplayTest:
        return new (std::nothrow) DisplayTestPage();
    default:
        return NULL;
    }
}

}  // namespace app

/* ------------------------------------------------------------------------ */
/* C entry points                                                            */
/* ------------------------------------------------------------------------ */

extern "C" esp_err_t app_manager_init(void)
{
    return app::AppManager::instance().init();
}

extern "C" void app_manager_run(void)
{
    app::AppManager::instance().run();
}
