/**
 * @file page.h
 * @brief Base class for every screen of the application.
 *
 * Lifecycle contract (AppManager is the only caller):
 *
 *   create(parent)   build the widget tree under `parent`; must set root_
 *   on_enter()       the page became visible - start timers, refresh data
 *   on_leave()       the page is about to be hidden - stop timers, stop work
 *   destroy()        delete root_; LVGL frees the whole child tree with it
 *
 * The split between on_leave() and destroy() matters even though AppManager
 * currently tears a page down as soon as it is left: on_leave() is where a
 * page releases things that are *not* part of its widget tree (timers, open
 * files, service subscriptions), and it runs before the objects disappear.
 *
 * See app_manager.h for why pages are not cached.
 *
 * Note on the signature: the task spec shows create()/destroy() without
 * arguments.  create() here takes the parent object because a page must never
 * assume which screen it is being mounted on - that is AppManager's business,
 * and it is what makes the page testable on its own.
 */
#pragma once

#include "lvgl.h"

class Page {
public:
    virtual ~Page() = default;

    /** @brief Stable, human readable name (also used in logs). */
    virtual const char *name() const = 0;

    /**
     * @brief Build the page widget tree.
     * @param parent usually the active screen
     *
     * Implementations must assign the top level container to root_.
     */
    virtual void create(lv_obj_t *parent) = 0;

    /** @brief Delete the page widget tree and drop the root reference. */
    virtual void destroy() = 0;

    /** @brief Called after the page became visible. */
    virtual void on_enter() {}

    /** @brief Called before the page is hidden. */
    virtual void on_leave() {}

    /** @brief The page container, NULL until create() has run. */
    lv_obj_t *root() const { return root_; }

protected:
    lv_obj_t *root_ = nullptr;
};
