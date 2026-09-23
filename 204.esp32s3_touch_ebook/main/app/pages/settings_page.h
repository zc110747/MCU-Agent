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
#include "net_service.h"
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

    /* Date & Time: the one place that writes the RTC by hand. */
    static void datetime_cb(lv_event_t *e);

    /* WiFi setup overlay.  Two steps in one overlay rather than a pushed page:
     * the access point someone picks is only meaningful next to the list they
     * picked it from, and Home is not where they want to land if they change
     * their mind. */
    static void wifi_row_cb(lv_event_t *e);        /* an access point was tapped  */
    static void wifi_rescan_cb(lv_event_t *e);
    static void wifi_forget_cb(lv_event_t *e);
    static void wifi_close_cb(lv_event_t *e);
    static void wifi_back_cb(lv_event_t *e);       /* password step -> list       */
    static void wifi_connect_cb(lv_event_t *e);
    static void wifi_ready_cb(lv_event_t *e);      /* keyboard OK                 */
    static void net_poll_tick(lv_timer_t *t);

    void open_wifi_overlay();
    void close_wifi_overlay();
    void wifi_show_list();
    void wifi_show_password();

    lv_obj_t *build_display_card(lv_obj_t *parent);
    lv_obj_t *build_network_card(lv_obj_t *parent);
    lv_obj_t *build_storage_card(lv_obj_t *parent);
    lv_obj_t *build_system_card(lv_obj_t *parent);
    lv_obj_t *build_about_card(lv_obj_t *parent);
    lv_obj_t *build_datetime_card(lv_obj_t *parent);

    /** @brief Re-read the SD card and the network state into the rows. */
    void refresh();
    void refresh_display();
    void refresh_storage();
    void refresh_network();
    void refresh_datetime();

    /** @brief Style a stock LVGL widget (keyboard, textarea) from Theme. */
    static void theme_keyboard(lv_obj_t *kb);

    /** @brief Flash a short line in the footer, in place of a modal dialog. */
    void notify(const char *text);

    lv_obj_t *brightness_row_ = nullptr;
    lv_obj_t *net_rows_[4] = {};
    lv_obj_t *storage_rows_[3] = {};
    lv_obj_t *datetime_val_[6] = {};   /* Year, Month, Day, Hour, Minute, Second */
    lv_obj_t *footer_note_ = nullptr;
    lv_timer_t *note_timer_ = nullptr;

    /* --- WiFi overlay state --- */
    lv_obj_t *wifi_overlay_ = nullptr;
    lv_obj_t *wifi_body_ = nullptr;
    lv_obj_t *wifi_summary_ = nullptr;
    lv_obj_t *wifi_password_ = nullptr;
    char      wifi_target_[services::kWifiSsidMax] = {};
    int       wifi_step_ = 0;          /* 0 = pick an AP, 1 = type its password */

    /* Polled rather than pushed: the state changes on the esp_event task, and
     * waking the LVGL task for every transition would be a redraw storm during
     * association.  shown_gen_ gates the redraw so a tick with nothing new to
     * say touches no widget at all, and scan_was_busy_ turns "a scan finished"
     * into an edge instead of a level. */
    lv_timer_t *poll_timer_ = nullptr;
    uint32_t    shown_gen_ = 0;
    bool        scan_was_busy_ = false;
};
