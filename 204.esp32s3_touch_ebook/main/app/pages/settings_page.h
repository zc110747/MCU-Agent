/**
 * @file settings_page.h
 * @brief Phase 12: Display / Network / Storage / System / About.
 *
 * There is no Rotation entry anywhere on this page, and that is a requirement
 * rather than an omission: the UI is fixed at 800x480 landscape and the
 * application layer has no business-level rotation.  The panel driver does not
 * expose one, so there is nothing for a control here to control - which is
 * exactly why it must not be drawn.
 */
#pragma once

#include "lvgl.h"
#include "page.h"

class SettingsPage : public Page {
public:
    const char *name() const override { return "settings"; }

    void create(lv_obj_t *parent) override;
    void destroy() override;
    void on_enter() override;
    void on_leave() override;

private:
    static void restart_cb(lv_event_t *e);
    static void brightness_cb(lv_event_t *e);
    static void rescan_cb(lv_event_t *e);
    static void net_action_cb(lv_event_t *e);
    static void note_tick(lv_timer_t *t);

    lv_obj_t *build_display_card(lv_obj_t *parent);
    lv_obj_t *build_network_card(lv_obj_t *parent);
    lv_obj_t *build_storage_card(lv_obj_t *parent);
    lv_obj_t *build_system_card(lv_obj_t *parent);
    lv_obj_t *build_about_card(lv_obj_t *parent);

    /** @brief Re-read the SD card and the network state into the rows. */
    void refresh();
    void refresh_display();
    void refresh_storage();
    void refresh_network();

    /** @brief Flash a short line in the footer, in place of a modal dialog. */
    void notify(const char *text);

    lv_obj_t *brightness_row_ = nullptr;
    lv_obj_t *net_rows_[3] = {};
    lv_obj_t *storage_rows_[3] = {};
    lv_obj_t *footer_note_ = nullptr;
    lv_timer_t *note_timer_ = nullptr;
};
