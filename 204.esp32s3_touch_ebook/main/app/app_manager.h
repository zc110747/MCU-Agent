/**
 * @file app_manager.h
 * @brief Page registry, navigation and the application task.
 *
 * NAVIGATION MODEL
 * ----------------
 * Exactly one page is alive at any moment: pushing a new page tears the old
 * one down.  That is a deliberate choice, not laziness - the task spec demands
 * that "Home -> Reader -> Home" provably releases the Reader widgets, and the
 * only way to prove that is to actually delete them and watch the LVGL object
 * count come back to its baseline.  Caching pages would make the leak test
 * meaningless.  If a page later turns out to be expensive to rebuild, we
 * revisit this with measurements in hand.
 *
 * THREADING
 * ---------
 * Every method here mutates LVGL state, so the caller must hold the LVGL
 * mutex (lvgl_port_acquire/release).  app_manager_init() is called from
 * app_main(), app_manager_run() only sleeps and logs.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"
#include "page.h"

namespace app {

/** @brief Every page of the application. Order is the navigation order. */
enum class PageId : uint8_t {
    DisplayTest = 0,   /* Phase 1: LCD bring-up test page */
    /* Phase 2+: Home, Reader, FileManager, Photos, Notes, Drawing,
     *           Clock, Calendar, Weather, Settings */
    Count
};

class AppManager {
public:
    /** Maximum navigation depth (Home + 7 levels of drill-down). */
    static constexpr int kMaxDepth = 8;

    static AppManager &instance();

    /** @brief Create the screen, the theme and the first page. Lock held. */
    esp_err_t init();

    /** @brief Application task body. Returns only on request_restart(). */
    void run();

    /** @brief Ask run() to return, e.g. after a settings change. */
    void request_restart();

    /** @brief Navigate to @p id, appending to the history. Lock held. */
    void push(PageId id);

    /** @brief Go one step back in history. No-op at the root page. */
    void pop();

    /** @brief The visible page, or NULL before init(). */
    Page *current() const { return current_; }

    /** @brief Current history depth (1 == root page). */
    int depth() const { return depth_; }

private:
    AppManager() = default;
    ~AppManager() = default;
    AppManager(const AppManager &) = delete;
    AppManager &operator=(const AppManager &) = delete;

    /** @brief Tear the current page down and build @p id in its place. */
    void show(PageId id);

    /** @brief Factory for one page. Returns NULL for an unhandled id. */
    Page *instantiate(PageId id) const;

    lv_obj_t *screen_ = nullptr;
    Page *current_ = nullptr;
    PageId history_[kMaxDepth] = {};
    int depth_ = 0;
    volatile bool restart_req_ = false;
};

}  // namespace app

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief C entry point used by main.cpp.
 *
 * Creates the root screen, applies the theme and shows the first page.
 * The LVGL lock must already be held by the caller.
 */
esp_err_t app_manager_init(void);

/**
 * @brief Application task body. Blocks until a restart is requested, then
 *        returns so main.cpp can call esp_restart().
 */
void app_manager_run(void);

#ifdef __cplusplus
}
#endif
