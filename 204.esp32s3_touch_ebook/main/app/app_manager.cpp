/**
 * @file app_manager.cpp
 * @brief Page registry, navigation and the application task.
 */

#include "app_manager.h"

#include <new>
#include <stdio.h>

#include "calendar_page.h"
#include "clock_page.h"
#include "display_test_page.h"
#include "drawing_page.h"
#include "file_manager_page.h"
#include "home_page.h"
#include "lvgl_port.h"
#include "notes_page.h"
#include "photos_page.h"
#include "reader_page.h"
#include "settings_page.h"
#include "system_info.h"
#include "theme.h"
#include "touch_test_page.h"
#include "weather_page.h"
#include "widgets.h"

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

/* NOTE: an acceptance build used to add three pieces of instrumentation here
 * under the EBOOK_ACCEPTANCE_SWEEP define - a per-page geometry dump, a
 * calibration frame painted on lv_layer_top(), and a render-rate probe.  All
 * three are gone.  The panel shift they were built to diagnose turned out to be
 * a DMA bandwidth fault (README.md, "LCD Configuration") and the page geometry
 * was correct all along.  The calibration frame especially must not ship: it is
 * drawn above every page, so it is visible on the glass.  If a layout question
 * ever needs the same treatment, the git history has both the code and the
 * define. */

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

    /* Header close button -> back to Home.  go_home() is the async form on
     * purpose: the handler runs inside an LVGL click event, and rebuilding the
     * page synchronously would delete the widget tree the event is still being
     * dispatched through.  Nothing is lost by the deferral. */
    ui::set_page_close_handler(go_home);

    /* LVGL shows the performance overlay automatically whenever
     * LV_USE_PERF_MONITOR is compiled in (lv_display.c: show_performance() on
     * display create).  Leaving it on costs a permanent "8% CPU / 30 ms" badge
     * in the corner of a consumer product, so it is hidden here. */
    lv_sysmon_hide_performance(NULL);

    screen_ = lv_screen_active();
    lv_obj_remove_style_all(screen_);
    lv_obj_add_style(screen_, Theme::screen(), 0);
    /* lv_obj_clear_flag() takes exactly one flag, so this is two calls - the
     * OR'd form only compiles in C, not C++. */
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLL_ELASTIC);

    if (depth_ == 0) {
        push(PageId::Home);
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

void AppManager::reset_to(PageId id)
{
    depth_ = 0;
    push(id);
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
    case PageId::Home:        return new (std::nothrow) HomePage();
    case PageId::Reader:      return new (std::nothrow) ReaderPage();
    case PageId::Photos:      return new (std::nothrow) PhotosPage();
    case PageId::Notes:       return new (std::nothrow) NotesPage();
    case PageId::Weather:     return new (std::nothrow) WeatherPage();
    case PageId::Clock:       return new (std::nothrow) ClockPage();
    case PageId::Calendar:    return new (std::nothrow) CalendarPage();
    case PageId::Drawing:     return new (std::nothrow) DrawingPage();
    case PageId::FileManager: return new (std::nothrow) FileManagerPage();
    case PageId::Settings:    return new (std::nothrow) SettingsPage();
    case PageId::TouchTest:   return new (std::nothrow) TouchTestPage();
    case PageId::DisplayTest: return new (std::nothrow) DisplayTestPage();
    default:                  return NULL;
    }
}

const char *page_id_name(PageId id)
{
    switch (id) {
    case PageId::Home:        return "home";
    case PageId::Reader:      return "reader";
    case PageId::Photos:      return "photos";
    case PageId::Notes:       return "notes";
    case PageId::Weather:     return "weather";
    case PageId::Clock:       return "clock";
    case PageId::Calendar:    return "calendar";
    case PageId::Drawing:     return "drawing";
    case PageId::FileManager: return "file_manager";
    case PageId::Settings:    return "settings";
    case PageId::TouchTest:   return "touch_test";
    case PageId::DisplayTest: return "display_test";
    default:                  return "?";
    }
}

/* ------------------------------------------------------------------------ */
/* deferred navigation                                                       */
/* ------------------------------------------------------------------------ */

static void do_push(void *arg)
{
    AppManager::instance().push(static_cast<PageId>(reinterpret_cast<uintptr_t>(arg)));
}

static void do_pop(void *)
{
    AppManager::instance().pop();
}

static void do_home(void *)
{
    AppManager::instance().reset_to(PageId::Home);
}

void navigate(PageId id)
{
    /* Deferred on purpose - see the note in app_manager.h. */
    lv_async_call(do_push, reinterpret_cast<void *>(static_cast<uintptr_t>(id)));
}

void go_back()
{
    lv_async_call(do_pop, NULL);
}

void go_home()
{
    lv_async_call(do_home, NULL);
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
