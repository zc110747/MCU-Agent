/**
 * @file app_manager.cpp
 * @brief Page registry, navigation and the application task.
 */

#include "app_manager.h"

#include <atomic>
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

#ifdef EBOOK_ACCEPTANCE_SWEEP
/* ---------------------------------------------------------------------------
 * TEMPORARY layout probe -- REMOVE TOGETHER WITH THE SWEEP BELOW
 * ---------------------------------------------------------------------------
 * `偏移` cannot be diagnosed from a handheld photograph: a panel-timing fault
 * (wrong blanking -> the whole image is shifted) and a layout fault (correct
 * panel, wrong object coordinates) look identical in a picture.  So instead of
 * guessing, this build prints the authoritative LVGL numbers AND paints a
 * calibration frame, which separates the two cases in one photo:
 *
 *   frame flush with the glass + content inset  -> layout fault, LVGL numbers
 *                                                 below say which object moved
 *   frame itself inset / clipped / wrapped      -> panel timing fault, tune the
 *                                                 HSYNC_/VSYNC_ blanking in
 *                                                 board_config.h
 */
static void log_geometry(const char *what, const lv_obj_t *obj)
{
    if (obj == NULL) {
        ESP_LOGI(TAG, "GEOM %-10s <null>", what);
        return;
    }
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    ESP_LOGI(TAG, "GEOM %-10s x=%3d..%3d  y=%3d..%3d  %3dx%3d", what,
             (int)a.x1, (int)a.x2, (int)a.y1, (int)a.y2,
             (int)(a.x2 - a.x1 + 1), (int)(a.y2 - a.y1 + 1));
}

/* One frame + four corner blocks + a centre cross, drawn on the top layer so
 * nothing a page does can hide it. */
static void draw_calibration_frame(void)
{
    static const uint32_t kFill[4] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00};
    static const lv_align_t kCorner[4] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT,
                                         LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT};
    lv_obj_t *layer = lv_layer_top();

    lv_obj_t *frame = lv_obj_create(layer);
    lv_obj_remove_style_all(frame);
    lv_obj_add_flag(frame, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(frame, LV_PCT(100), LV_PCT(100));
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0xFF00FF), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(frame, 0, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);

    for (int i = 0; i < 4; ++i) {
        lv_obj_t *b = lv_obj_create(layer);
        lv_obj_remove_style_all(b);
        lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(b, 40, 40);
        lv_obj_set_style_bg_color(b, lv_color_hex(kFill[i]), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_align(b, kCorner[i], 0, 0);
    }

    lv_obj_t *cross_h = lv_obj_create(layer);
    lv_obj_t *cross_v = lv_obj_create(layer);
    lv_obj_t *crosses[2] = {cross_h, cross_v};
    for (lv_obj_t *c : crosses) {
        lv_obj_remove_style_all(c);
        lv_obj_add_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(c, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(c, 0, 0);
    }
    lv_obj_set_size(cross_h, 60, 2);
    lv_obj_set_size(cross_v, 2, 60);
    lv_obj_align(cross_h, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(cross_v, LV_ALIGN_CENTER, 0, 0);
}

/* Render-rate probe.
 *
 * "The touch feels laggy" is not measurable by eye, and the two candidate
 * causes pull in opposite directions: a slow render pipeline (LVGL stalled on
 * PSRAM bandwidth) and a slow input path (touch only sampled every refresh
 * period) both feel the same.  Counting renders per second separates them.
 *
 * LV_EVENT_RENDER_READY is the right event: unlike LV_EVENT_REFR_READY it is
 * only sent for a cycle that actually painted something, so an idle screen
 * reports 0 rather than the timer rate.  The counter is written by the LVGL
 * task and read by the application task, so it is a real cross-task counter:
 * std::atomic, not volatile.  (C++20 deprecated `++` on a volatile-qualified
 * type precisely because volatile says nothing about atomicity, and this
 * project builds with -Werror.)
 */
static std::atomic<uint32_t> s_renders{0};

static void on_display_render_ready(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_RENDER_READY) {
        s_renders.fetch_add(1, std::memory_order_relaxed);
    }
}

uint32_t renders_take(void)
{
    /* exchange() rather than read-then-clear: a render landing between the two
     * would be silently dropped from both windows. */
    return s_renders.exchange(0, std::memory_order_relaxed);
}
#endif  // EBOOK_ACCEPTANCE_SWEEP

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

#if !defined(EBOOK_ACCEPTANCE_SWEEP)
    /* LVGL shows the performance overlay automatically whenever
     * LV_USE_PERF_MONITOR is compiled in (lv_display.c: show_performance() on
     * display create).  Leaving it on costs a permanent "8% CPU / 30 ms" badge
     * in the corner of a consumer product, so it is hidden here and only
     * re-enabled for an acceptance build, where the numbers are the point. */
    lv_sysmon_hide_performance(NULL);
#endif

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

#ifdef EBOOK_ACCEPTANCE_SWEEP
    /* =====================================================================
     * TEMPORARY ACCEPTANCE SWEEP -- REMOVE BEFORE DELIVERY
     * =====================================================================
     * Verifies, on real hardware and with no human tapping, that every page
     * in the closed page set can be built and torn down:
     *   - each page's create() returns a root and does not crash;
     *   - the LVGL object count returns to the Home baseline after the whole
     *     sweep, which is the machine-checkable form of "Home -> Reader ->
     *     Home releases the Reader widgets";
     *   - heap before/after is reported so a leak shows up in bytes too.
     * It exercises the shipped page code unchanged; only the driving differs.
     * The two lines that enable it -- this block and the matching
     * target_compile_definitions() in main/CMakeLists.txt -- are deleted once
     * the log is captured, so the delivered firmware contains no test code.
     * ===================================================================== */
    {
        system_info_log_memory("SWEEP");
        const uint32_t baseline = count_objects(screen_);
        ESP_LOGW(TAG, "SWEEP baseline  page 'home'  objects = %u", (unsigned)baseline);

        PageId heaviest = PageId::Home;
        uint32_t heaviest_n = baseline;
        int failures = 0;

        for (uint8_t i = 0; i < static_cast<uint8_t>(PageId::Count); ++i) {
            const PageId id = static_cast<PageId>(i);
            show(id);

            const bool built = (current_ != NULL) && (current_->root() != NULL);
            const uint32_t n = count_objects(screen_);
            if (!built) {
                ++failures;
                ESP_LOGE(TAG, "SWEEP FAIL    page '%-11s' did not build", page_id_name(id));
                continue;
            }
            if (n > heaviest_n) {
                heaviest_n = n;
                heaviest = id;
            }
            ESP_LOGW(TAG, "SWEEP ok      page '%-11s' objects = %u", page_id_name(id), (unsigned)n);

            /* Per-page geometry: root, then its direct children (for a page
             * built by ui::page_layout() that is header / body / footer).
             * lv_obj_update_layout() is mandatory here: LVGL computes an
             * object's coordinates lazily during a refresh, so reading them in
             * the same cycle a page was built returns 0..-1 / 0x0 - a perfectly
             * valid "not laid out yet", and a very convincing fake "the whole
             * page is zero sized" bug. */
            lv_obj_update_layout(screen_);
            log_geometry("screen", screen_);
            log_geometry("  root", current_->root());
            const uint32_t kids = lv_obj_get_child_count(current_->root());
            for (uint32_t k = 0; k < kids; ++k) {
                char tag[24];
                snprintf(tag, sizeof(tag), "  child[%u]", (unsigned)k);
                log_geometry(tag, lv_obj_get_child(current_->root(), (int32_t)k));
            }
        }

        reset_to(PageId::Home);
        const uint32_t after = count_objects(screen_);
        ESP_LOGW(TAG, "SWEEP heaviest page '%-11s' objects = %u",
                 page_id_name(heaviest), (unsigned)heaviest_n);
        ESP_LOGW(TAG, "SWEEP result  objects = %u vs baseline %u  failures = %d  -> %s",
                 (unsigned)after, (unsigned)baseline, failures,
                 (failures == 0 && after == baseline) ? "PASS" : "FAIL");
        system_info_log_memory("SWEEP");

        /* Draw the calibration frame last so it survives on top of Home.
         * The board's own H/V values are printed by the platform layer
         * (system_info_log_panel), so the two log lines can be compared
         * without the app layer having to include a platform header. */
        lv_display_t *disp = lv_display_get_default();
        ESP_LOGW(TAG, "GEOM display  %dx%d  dpi=%d  colour=%d bpp",
                 (int)lv_display_get_horizontal_resolution(disp),
                 (int)lv_display_get_vertical_resolution(disp),
                 (int)lv_display_get_dpi(disp),
                 (int)LV_COLOR_DEPTH);
        draw_calibration_frame();
        ESP_LOGW(TAG, "GEOM calibration frame drawn on lv_layer_top()");

        /* Start the render-rate probe.  Registered after the sweep so its
         * count is not polluted by twelve page builds in a row. */
        lv_display_add_event_cb(disp, on_display_render_ready, LV_EVENT_RENDER_READY, NULL);
    }
#endif

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
#ifdef EBOOK_ACCEPTANCE_SWEEP
    /* Separate, much shorter window than the memory heartbeat: a leak takes
     * minutes to become visible, a frame rate takes seconds to settle. */
    static constexpr uint32_t kRateWindowMs = 5000;
    uint32_t rate_ms = 0;
#endif

    while (!restart_req_) {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed_ms += 100;
#ifdef EBOOK_ACCEPTANCE_SWEEP
        rate_ms += 100;
        if (rate_ms >= kRateWindowMs) {
            const uint32_t n = renders_take();
            ESP_LOGW(TAG, "RATE renders = %u in %u ms -> %u/s",
                     (unsigned)n, (unsigned)rate_ms,
                     (unsigned)((n * 1000u) / rate_ms));
            rate_ms = 0;
        }
#endif
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
