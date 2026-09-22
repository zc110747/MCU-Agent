/**
 * @file settings_page.cpp
 * @brief Phase 12: Display / Network / Storage / System / About.
 *
 * Layout: five cards in one vertically scrollable body.  Cards are used rather
 * than a flat list because the sections have genuinely different shapes - the
 * About block is read-only text, the Network block has actions - and a single
 * undifferentiated list of rows would hide that.
 */

#include "settings_page.h"

#include <stdio.h>
#include <string.h>

#include "app_icons.h"
#include "app_manager.h"
#include "clock_service.h"
#include "display_driver.h"
#include "net_service.h"
#include "storage_service.h"
#include "system_info.h"
#include "theme.h"
#include "widgets.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "lvgl.h"

static const char *TAG = "settings";

/* The footer note is a lightweight substitute for a modal: it shows why an
 * action did nothing, which is strictly more useful than a dialog whose only
 * button is "OK". */
static constexpr uint32_t kNoteMs = 2600;

void SettingsPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Settings", true);
    root_ = page.root;

    lv_obj_t *hint = lv_label_create(page.header_right);
    lv_obj_add_style(hint, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(hint, Theme::font_small(), 0);
    lv_label_set_text(hint, "800x480 landscape, fixed");

    lv_obj_t *body = page.body;
    /* One column of cards; the body itself scrolls when the cards do not fit. */
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, Theme::kGapLg, 0);

    build_display_card(body);
    build_network_card(body);
    build_storage_card(body);
    build_system_card(body);
    build_about_card(body);

    footer_note_ = lv_label_create(page.footer_left);
    lv_obj_add_style(footer_note_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(footer_note_, Theme::font_small(), 0);
    lv_label_set_text(footer_note_, "");

    ui::app_button(page.footer_right, "Back", back_cb, nullptr);

    ESP_LOGI(TAG, "settings built");
}

/* ------------------------------------------------------------------------ */
/* Display                                                                  */
/* ------------------------------------------------------------------------ */

lv_obj_t *SettingsPage::build_display_card(lv_obj_t *parent)
{
    lv_obj_t *card = ui::app_card(parent, "Display");

    brightness_row_ = ui::info_row(card, "Brightness", "--");
    /* Tappable so the backlight can actually be toggled: that is the whole of
     * what this board supports, and a row that only reports the state while
     * pretending there is more would be the dishonest version. */
    lv_obj_add_flag(brightness_row_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(brightness_row_, brightness_cb, LV_EVENT_CLICKED, this);

    ui::info_row(card, "Resolution", "800x480 RGB565");

    /* The panel's LED string is driven by an AP3032 boost converter whose CTRL
     * pin is a plain expander output - there is no PWM line on this board.  So
     * there is no brightness *level* to offer, and saying so is better than
     * showing a slider that does nothing.  The note is part of the UI, not a
     * comment, because a user will look for the slider. */
    lv_obj_t *note = lv_label_create(card);
    lv_obj_add_style(note, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(note, Theme::font_small(), 0);
    lv_label_set_text(note, "This panel has no PWM dimming line: the backlight "
                            "is on or off only.");
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);

    return card;
}

void SettingsPage::refresh_display()
{
    ui::info_row_set(brightness_row_,
                     display_backlight_get() ? "On  (tap to switch off)"
                                             : "Off (tap to switch on)");
}

/* ------------------------------------------------------------------------ */
/* Network                                                                  */
/* ------------------------------------------------------------------------ */

lv_obj_t *SettingsPage::build_network_card(lv_obj_t *parent)
{
    lv_obj_t *card = ui::app_card(parent, "Network");

    net_rows_[0] = ui::info_row(card, "WiFi", "--");
    net_rows_[1] = ui::info_row(card, "SSID", "--");
    net_rows_[2] = ui::info_row(card, "IP address", "--");

    lv_obj_t *actions = lv_obj_create(card);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, Theme::kGapSm, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    /* Connect and Disconnect exist as controls because the specification names
     * them; they report that there are no credentials to use yet.  A button
     * that lies about connecting would be worse than one that explains. */
    ui::app_button(actions, "Connect", net_action_cb, this);
    ui::app_button(actions, "Disconnect", net_action_cb, this);
    ui::app_button(actions, "Scan", net_action_cb, this);

    return card;
}

void SettingsPage::refresh_network()
{
    const services::WifiState st = services::net_wifi_state();
    ui::info_row_set(net_rows_[0], services::net_wifi_state_text(st));
    ui::info_row_set(net_rows_[1], services::net_ssid());
    ui::info_row_set(net_rows_[2], services::net_ip());
}

/* ------------------------------------------------------------------------ */
/* Storage                                                                  */
/* ------------------------------------------------------------------------ */

lv_obj_t *SettingsPage::build_storage_card(lv_obj_t *parent)
{
    lv_obj_t *card = ui::app_card(parent, "Storage");

    storage_rows_[0] = ui::info_row(card, "SD card", "--");
    storage_rows_[1] = ui::info_row(card, "Capacity", "--");
    storage_rows_[2] = ui::info_row(card, "Free space", "--");

    lv_obj_t *actions = lv_obj_create(card);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    ui::app_button(actions, "Re-scan card", rescan_cb, this);

    return card;
}

void SettingsPage::refresh_storage()
{
    if (!services::storage_ready()) {
        ui::info_row_set(storage_rows_[0], "not mounted");
        ui::info_row_set(storage_rows_[1], "--");
        ui::info_row_set(storage_rows_[2], "--");
        return;
    }

    ui::info_row_set(storage_rows_[0], "mounted at /sd");

    uint64_t total = 0, free_bytes = 0;
    if (services::storage_capacity(&total, &free_bytes) != ESP_OK) {
        ui::info_row_set(storage_rows_[1], "unavailable");
        ui::info_row_set(storage_rows_[2], "unavailable");
        return;
    }

    char buf[24];
    services::storage_human_size(buf, sizeof(buf), total);
    ui::info_row_set(storage_rows_[1], buf);
    services::storage_human_size(buf, sizeof(buf), free_bytes);
    ui::info_row_set(storage_rows_[2], buf);
}

/* ------------------------------------------------------------------------ */
/* System                                                                   */
/* ------------------------------------------------------------------------ */

lv_obj_t *SettingsPage::build_system_card(lv_obj_t *parent)
{
    lv_obj_t *card = ui::app_card(parent, "System");

    /* Restart is a real restart: AppManager::run() returns and main.cpp calls
     * esp_restart(), so there is no half-restarted state to leave behind. */
    ui::list_item(card, &icon_refresh, "Restart device",
                  "reboots the ESP32-S3", false, restart_cb, nullptr);

    return card;
}

/* ------------------------------------------------------------------------ */
/* About                                                                    */
/* ------------------------------------------------------------------------ */

lv_obj_t *SettingsPage::build_about_card(lv_obj_t *parent)
{
    lv_obj_t *card = ui::app_card(parent, "About");

    system_info_t info;
    system_info_collect(&info);

    char chip[64];
    snprintf(chip, sizeof(chip), "%s @ %u MHz",
             info.chip_model ? info.chip_model : "ESP32-S3",
             (unsigned)info.cpu_freq_mhz);

    ui::info_row(card, "Project", "Ebook LVGL");
    /* The version comes from the application descriptor, i.e. the value the
     * running image was actually built with - PROJECT_VER in the top-level
     * CMakeLists.txt.  Reading it from the image instead of pasting a literal
     * here is what stops the two from drifting apart. */
    ui::info_row(card, "Version", esp_app_get_description()->version);
    ui::info_row(card, "Chip", chip);
    ui::info_row(card, "LVGL", lv_version_info());
    ui::info_row(card, "ESP-IDF", esp_get_idf_version());
    ui::info_row(card, "Flash / PSRAM", "16 MB / 8 MB");

    return card;
}

/* ------------------------------------------------------------------------ */
/* lifecycle                                                                */
/* ------------------------------------------------------------------------ */

void SettingsPage::destroy()
{
    if (note_timer_ != nullptr) {
        lv_timer_delete(note_timer_);
        note_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    brightness_row_ = nullptr;
    footer_note_ = nullptr;
    for (int i = 0; i < 3; ++i) {
        net_rows_[i] = nullptr;
        storage_rows_[i] = nullptr;
    }
}

void SettingsPage::on_enter()
{
    refresh();
}

void SettingsPage::on_leave()
{
    if (note_timer_ != nullptr) {
        lv_timer_delete(note_timer_);
        note_timer_ = nullptr;
    }
}

void SettingsPage::refresh()
{
    refresh_display();
    refresh_network();
    refresh_storage();
}

void SettingsPage::notify(const char *text)
{
    if (footer_note_ == nullptr) {
        return;
    }
    lv_label_set_text(footer_note_, text ? text : "");
    if (note_timer_ != nullptr) {
        lv_timer_delete(note_timer_);
    }
    note_timer_ = lv_timer_create(note_tick, kNoteMs, this);
}

void SettingsPage::note_tick(lv_timer_t *timer)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        if (self->footer_note_ != nullptr) {
            lv_label_set_text(self->footer_note_, "");
        }
        if (self->note_timer_ != nullptr) {
            lv_timer_delete(self->note_timer_);
            self->note_timer_ = nullptr;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* actions                                                                  */
/* ------------------------------------------------------------------------ */

void SettingsPage::brightness_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    const bool next = !display_backlight_get();
    if (display_backlight_set(next) == ESP_OK) {
        if (self != nullptr) {
            self->refresh_display();
            self->notify(next ? "backlight on" : "backlight off");
        }
    } else if (self != nullptr) {
        self->notify("backlight control failed");
    }
}

void SettingsPage::rescan_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    /* storage_init() is idempotent: it returns immediately when a card is
     * already mounted, so this is safe to tap repeatedly. */
    const esp_err_t err = services::storage_init();
    if (self != nullptr) {
        self->refresh_storage();
        self->notify(err == ESP_OK ? "card mounted" : "no card found");
    }
}

void SettingsPage::net_action_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));

    lv_obj_t *btn = lv_event_get_target_obj(e);
    const char *label = "-";
    if (btn != nullptr) {
        lv_obj_t *child = lv_obj_get_child(btn, 0);
        if (child != nullptr) {
            label = lv_label_get_text(child);
        }
    }

    if (self == nullptr) {
        return;
    }

    if (strcmp(label, "Scan") == 0) {
        const esp_err_t err = services::net_scan_start();
        self->notify(err == ESP_OK ? "scanning..." : "scan unavailable: no WiFi in this build");
    } else {
        /* Both Connect and Disconnect land here.  There is nothing to connect
         * *with* yet, so the honest answer is to say so and point at the fix
         * rather than silently doing nothing. */
        self->notify("no credentials stored (WiFi bring-up is a separate step)");
    }
    self->refresh_network();
}

void SettingsPage::restart_cb(lv_event_t *)
{
    ESP_LOGW(TAG, "restart requested from Settings");
    app::AppManager::instance().request_restart();
}

void SettingsPage::back_cb(lv_event_t *)
{
    app::go_back();
}
