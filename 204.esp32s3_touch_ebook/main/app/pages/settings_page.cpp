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

namespace {

/* Which date/time field a stepper button edits, packed with its direction so a
 * single handler can serve all twelve buttons.  Field in the high bits, sign in
 * the low bit. */
enum class DtField : int { Year, Month, Day, Hour, Minute, Second };

constexpr intptr_t dtcode(DtField f, int d)
{
    return (static_cast<intptr_t>(f) << 1) | (d > 0 ? 1 : 0);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Wrap within [lo, hi] inclusive, so stepping a field past either end lands on
 * the other end instead of producing an out-of-range value the RTC would reject. */
static int wrap(int v, int lo, int hi)
{
    const int n = hi - lo + 1;
    int r = (v - lo) % n;
    if (r < 0) {
        r += n;
    }
    return lo + r;
}

}  // namespace

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
    build_datetime_card(body);
    build_system_card(body);
    build_about_card(body);

    footer_note_ = lv_label_create(page.footer_left);
    lv_obj_add_style(footer_note_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(footer_note_, Theme::font_small(), 0);
    lv_label_set_text(footer_note_, "");

    /* SetTime now lives at the bottom-right of the Date & Time card (see
     * build_datetime_card); the page footer is left free. */

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
    net_rows_[3] = ui::info_row(card, "Signal", "--");

    lv_obj_t *actions = lv_obj_create(card);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, Theme::kGapSm, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    ui::app_button(actions, "WiFi setup", net_action_cb, this);
    ui::app_button(actions, "Connect", net_action_cb, this);
    ui::app_button(actions, "Disconnect", net_action_cb, this);
    ui::app_button(actions, "Forget", net_action_cb, this);

    return card;
}

void SettingsPage::refresh_network()
{
    const services::WifiState st = services::net_wifi_state();
    const char *ssid = services::net_ssid();
    const char *ip   = services::net_ip();
    const int   rssi = services::net_rssi();

    ui::info_row_set(net_rows_[0], services::net_wifi_state_text(st));
    ui::info_row_set(net_rows_[1], (ssid != nullptr && ssid[0] != '\0') ? ssid : "--");
    ui::info_row_set(net_rows_[2], (strcmp(ip, "0.0.0.0") == 0) ? "--" : ip);

    /* 0 dBm is outside anything a real measurement can be, so it is the
     * service's "not associated" marker rather than a reading. */
    if (st == services::WifiState::Connected && rssi != 0) {
        const int bars = services::net_rssi_bars((int8_t)rssi);
        char signal[32];
        /* Text bars rather than a graphic: this is a 30 px row and the meaning
         * has to survive at that size. */
        snprintf(signal, sizeof(signal), "%d dBm  %.*s", rssi, bars, "||||");
        ui::info_row_set(net_rows_[3], signal);
    } else {
        ui::info_row_set(net_rows_[3], "--");
    }

    /* The generation the rows above were drawn from, so the poll timer does not
     * redraw them again on its next pass. */
    shown_gen_ = services::net_generation();
}

/* ------------------------------------------------------------------------ */
/* WiFi setup overlay                                                       */
/* ------------------------------------------------------------------------ */

void SettingsPage::theme_keyboard(lv_obj_t *kb)
{
    /* lv_keyboard is built on lv_buttonmatrix, so the keys are draw parts of
     * one object rather than child widgets - which is why they are styled
     * through the ITEMS selector and not by walking children.  Without this the
     * default theme paints a light keyboard in the middle of a dark page. */
    lv_obj_set_style_bg_color(kb, lv_color_hex(Theme::kSurface), LV_PART_MAIN);
    lv_obj_set_style_border_color(kb, lv_color_hex(Theme::kBorder), LV_PART_MAIN);
    lv_obj_set_style_border_width(kb, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(kb, Theme::kRadiusMd, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, Theme::kGapSm, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(kb, Theme::kGapXs, LV_PART_MAIN);

    const lv_style_selector_t items = LV_PART_ITEMS;
    lv_obj_set_style_bg_color(kb, lv_color_hex(Theme::kSurface2), items);
    lv_obj_set_style_bg_color(kb, lv_color_hex(Theme::kAccent), items | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(kb, lv_color_hex(Theme::kBorder), items);
    lv_obj_set_style_border_width(kb, 1, items);
    lv_obj_set_style_radius(kb, Theme::kRadiusSm, items);
    lv_obj_set_style_text_color(kb, lv_color_hex(Theme::kText), items);
    lv_obj_set_style_text_color(kb, lv_color_hex(Theme::kBg), items | LV_STATE_PRESSED);
    lv_obj_set_style_text_font(kb, Theme::font_body(), items);
}

void SettingsPage::open_wifi_overlay()
{
    if (wifi_overlay_ != nullptr) {
        return;
    }

    /* Full-page overlay as a child of the page root, exempt from the root's
     * flex column - same construction the file manager's preview uses. */
    wifi_overlay_ = lv_obj_create(root_);
    lv_obj_remove_style_all(wifi_overlay_);
    lv_obj_add_flag(wifi_overlay_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(wifi_overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(wifi_overlay_, 0, 0);
    lv_obj_set_style_bg_color(wifi_overlay_, lv_color_hex(Theme::kBg), 0);
    lv_obj_set_style_bg_opa(wifi_overlay_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(wifi_overlay_, LV_OBJ_FLAG_SCROLLABLE);

    /* with_close = false: this overlay is not a pushed page, and the header's
     * exit button goes Home - which would abandon the page the user is still
     * working in.  Its own Close in the footer is the dismiss that fits. */
    ui::PageLayout pv = ui::page_layout(wifi_overlay_, "WiFi setup", true, false);
    wifi_body_ = pv.body;
    lv_obj_set_flex_flow(wifi_body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_body_, Theme::kGapSm, 0);

    ui::app_button(pv.footer_left, "Forget", wifi_forget_cb, this);
    ui::app_button(pv.footer_right, "Close", wifi_close_cb, this);

    wifi_show_list();
}

void SettingsPage::close_wifi_overlay()
{
    if (wifi_overlay_ != nullptr) {
        lv_obj_delete(wifi_overlay_);
    }
    wifi_overlay_ = nullptr;
    wifi_body_ = nullptr;
    wifi_summary_ = nullptr;
    /* Deleting the textarea takes the keyboard with it: the keyboard is a child
     * of the body, but clearing the pointers here is what stops a later tick
     * from touching freed objects. */
    wifi_password_ = nullptr;
    wifi_step_ = 0;
    wifi_target_[0] = '\0';
}

void SettingsPage::wifi_show_list()
{
    wifi_step_ = 0;
    wifi_password_ = nullptr;
    lv_obj_clean(wifi_body_);

    wifi_summary_ = lv_label_create(wifi_body_);
    lv_obj_add_style(wifi_summary_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(wifi_summary_, Theme::font_small(), 0);
    lv_label_set_text(wifi_summary_, "");

    lv_obj_t *actions = lv_obj_create(wifi_body_);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, Theme::kGapSm, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    ui::app_button(actions, "Scan", wifi_rescan_cb, this);

    lv_obj_t *list = lv_obj_create(wifi_body_);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, Theme::kGapXs, 0);

    size_t count = 0;
    const services::WifiAp *aps = services::net_scan_results(&count);
    if (count == 0) {
        ui::empty_state(list, "..", "No networks listed yet",
                        "Tap Scan. Results appear here, strongest first.");
        lv_label_set_text(wifi_summary_, services::net_scan_busy() ? "scanning..." : "not scanned");
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        char subtitle[64];
        const int bars = services::net_rssi_bars(aps[i].rssi);
        snprintf(subtitle, sizeof(subtitle), "%d dBm  %.*s  %s", (int)aps[i].rssi, bars, "||||",
                 aps[i].secure ? "password" : "open");
        /* The index travels in the object's own user data and `this` in the
         * event's: list_item() has only one user slot, and it is needed for the
         * page.  Stored as index+1 so that a zeroed slot is never mistaken for
         * the first access point. */
        lv_obj_t *row = ui::list_item(list, nullptr, aps[i].ssid, subtitle, true,
                                     wifi_row_cb, this);
        lv_obj_set_user_data(row, (void *)(uintptr_t)(i + 1));
    }

    lv_label_set_text(wifi_summary_, services::net_scan_busy() ? "scanning..." : "pick a network");
}

void SettingsPage::wifi_show_password()
{
    wifi_step_ = 1;
    lv_obj_clean(wifi_body_);

    /* Which network this password is for: the list the user picked from is gone
     * at this step, so without this row they would be typing blind. */
    ui::info_row(wifi_body_, "Network", wifi_target_);

    wifi_summary_ = lv_label_create(wifi_body_);
    lv_obj_add_style(wifi_summary_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(wifi_summary_, Theme::font_small(), 0);
    lv_label_set_text(wifi_summary_, "type the password, then OK on the keyboard");

    wifi_password_ = lv_textarea_create(wifi_body_);
    lv_textarea_set_one_line(wifi_password_, true);
    lv_textarea_set_password_mode(wifi_password_, true);
    lv_textarea_set_placeholder_text(wifi_password_, "password (empty for an open network)");
    lv_obj_set_width(wifi_password_, LV_PCT(100));
    /* Themed from Theme rather than left to the default theme, same reason as
     * the keyboard above. */
    lv_obj_set_style_bg_color(wifi_password_, lv_color_hex(Theme::kSurface2), 0);
    lv_obj_set_style_border_color(wifi_password_, lv_color_hex(Theme::kBorder), 0);
    lv_obj_set_style_border_width(wifi_password_, 1, 0);
    lv_obj_set_style_radius(wifi_password_, Theme::kRadiusSm, 0);
    lv_obj_set_style_text_color(wifi_password_, lv_color_hex(Theme::kText), 0);
    lv_obj_set_style_text_font(wifi_password_, Theme::font_body(), 0);
    lv_obj_set_style_pad_all(wifi_password_, Theme::kGapSm, 0);

    lv_obj_t *actions = lv_obj_create(wifi_body_);
    lv_obj_remove_style_all(actions);
    lv_obj_set_width(actions, LV_PCT(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, Theme::kGapSm, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    ui::app_button(actions, "Connect", wifi_connect_cb, this);
    ui::app_button(actions, "Back", wifi_back_cb, this);

    lv_obj_t *kb = lv_keyboard_create(wifi_body_);
    lv_obj_set_width(kb, LV_PCT(100));
    /* Both a height and a grow, deliberately.  The height is the basis the
     * keyboard would keep on its own; the grow lets it absorb whatever the
     * column has left over, so on a shorter panel the Connect/Back row above
     * cannot be pushed off the bottom.  Removing the height would let the
     * keyboard's content decide its own basis, which is more rows than the
     * password step needs. */
    lv_obj_set_flex_grow(kb, 1);
    lv_obj_set_height(kb, 190);
    theme_keyboard(kb);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(kb, wifi_password_);
    lv_obj_add_event_cb(kb, wifi_ready_cb, LV_EVENT_READY, this);
}

void SettingsPage::wifi_rescan_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    const esp_err_t err = services::net_scan_start();
    if (self->wifi_summary_ != nullptr) {
        if (err == ESP_OK) {
            lv_label_set_text(self->wifi_summary_, "scanning...");
        } else {
            /* The error name, not a generic "unavailable": the only realistic
             * failure here is the stack not being up yet, and "ESP_ERR_..." is
             * what makes that diagnosable from a photograph of the screen. */
            char msg[64];
            snprintf(msg, sizeof(msg), "scan failed: %s", esp_err_to_name(err));
            lv_label_set_text(self->wifi_summary_, msg);
        }
    }
    if (err == ESP_OK) {
        self->refresh_network();
    }
}

void SettingsPage::wifi_row_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    lv_obj_t *row = lv_event_get_target_obj(e);
    if (self == nullptr || row == nullptr) {
        return;
    }
    const uintptr_t tag = (uintptr_t)lv_obj_get_user_data(row);
    if (tag == 0) {
        return;
    }
    size_t count = 0;
    const services::WifiAp *aps = services::net_scan_results(&count);
    const size_t idx = (size_t)tag - 1;
    if (idx >= count) {
        return;
    }
    snprintf(self->wifi_target_, sizeof(self->wifi_target_), "%s", aps[idx].ssid);
    self->wifi_show_password();
}

void SettingsPage::wifi_back_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->wifi_show_list();
    }
}

void SettingsPage::wifi_connect_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    if (self == nullptr || self->wifi_password_ == nullptr) {
        return;
    }
    const char *pass = lv_textarea_get_text(self->wifi_password_);
    const esp_err_t err = services::net_wifi_connect(self->wifi_target_, pass);
    if (err != ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "cannot connect: %s", esp_err_to_name(err));
        lv_label_set_text(self->wifi_summary_, msg);
        return;
    }
    lv_label_set_text(self->wifi_summary_, "connecting... watch the Network card");
    self->refresh_network();
}

void SettingsPage::wifi_ready_cb(lv_event_t *e)
{
    /* The keyboard's OK key is the same action as the Connect button, so it
     * goes through the same path rather than a second copy of it. */
    wifi_connect_cb(e);
}

void SettingsPage::wifi_forget_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    const esp_err_t err = services::net_wifi_forget();
    if (self != nullptr) {
        if (self->wifi_summary_ != nullptr) {
            lv_label_set_text(self->wifi_summary_,
                              err == ESP_OK ? "credentials erased" : "erase failed");
        }
        self->refresh_network();
    }
}

void SettingsPage::wifi_close_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->close_wifi_overlay();
        self->refresh_network();
    }
}

void SettingsPage::net_poll_tick(lv_timer_t *timer)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }

    const bool busy = services::net_scan_busy();

    /* The access point list is rebuilt only on the edge where a scan that was
     * running has just finished, and only while the list is the step on
     * screen.  Rebuilding it on a level test would rebuild it twice a second
     * for as long as the overlay stayed open. */
    if (self->wifi_overlay_ != nullptr && self->wifi_step_ == 0) {
        if (self->scan_was_busy_ && !busy) {
            self->wifi_show_list();
        } else if (busy && self->wifi_summary_ != nullptr) {
            lv_label_set_text(self->wifi_summary_, "scanning...");
        }
    }
    self->scan_was_busy_ = busy;

    /* Nothing new from the stack means nothing to redraw: three labels and a
     * row write per tick would keep the whole card invalidated for no reason. */
    const uint32_t gen = services::net_generation();
    if (gen != self->shown_gen_) {
        self->shown_gen_ = gen;
        self->refresh_network();
    }
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
/* Date & Time                                                              */
/* ------------------------------------------------------------------------ */

lv_obj_t *SettingsPage::build_datetime_card(lv_obj_t *parent)
{
    lv_obj_t *card = ui::app_card(parent, "Date & Time");

    /* Two rows of three fields: row 1 = Year / Month / Day, row 2 = Hour /
     * Minute / Second.  Each field is a self-contained cell (name + value + its
     * own - / +), and those steppers edit a DRAFT (draft_) - not the RTC.  Only
     * SetTime, in the footer, commits the draft. */
    lv_obj_t *grid = lv_obj_create(card);
    lv_obj_remove_style_all(grid);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    static const int32_t gcol[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                   LV_GRID_TEMPLATE_LAST};
    static const int32_t grow[] = {LV_GRID_CONTENT, LV_GRID_CONTENT,
                                   LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, gcol, grow);
    lv_obj_set_style_pad_row(grid, Theme::kGapMd, 0);
    lv_obj_set_style_pad_column(grid, Theme::kGapMd, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    static const char *const kNames[6] = {
        "Year", "Month", "Day", "Hour", "Minute", "Second"
    };

    for (int f = 0; f < 6; ++f) {
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_height(cell, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, Theme::kGapXs, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, f % 3, 1,
                             LV_GRID_ALIGN_STRETCH, f / 3, 1);

        lv_obj_t *name = lv_label_create(cell);
        lv_obj_add_style(name, Theme::text_dim(), 0);
        lv_obj_set_style_text_font(name, Theme::font_small(), 0);
        lv_label_set_text(name, kNames[f]);

        lv_obj_t *val = lv_label_create(cell);
        lv_obj_add_style(val, Theme::text_body(), 0);
        lv_obj_set_style_text_font(val, Theme::font_title(), 0);
        lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(val, LV_PCT(100));
        datetime_val_[f] = val;

        lv_obj_t *steps = lv_obj_create(cell);
        lv_obj_remove_style_all(steps);
        lv_obj_set_width(steps, LV_PCT(100));
        lv_obj_set_height(steps, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(steps, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(steps, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(steps, Theme::kGapSm, 0);
        lv_obj_set_style_pad_row(steps, 0, 0);
        lv_obj_clear_flag(steps, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *minus = ui::app_button(steps, "-", datetime_cb, this);
        lv_obj_set_user_data(minus, reinterpret_cast<void *>(
                                  dtcode(static_cast<DtField>(f), -1)));
        lv_obj_t *plus = ui::app_button(steps, "+", datetime_cb, this);
        lv_obj_set_user_data(plus, reinterpret_cast<void *>(
                                 dtcode(static_cast<DtField>(f), 1)));
    }

    lv_obj_t *note = lv_label_create(card);
    lv_obj_add_style(note, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(note, Theme::font_small(), 0);
    lv_label_set_text(note, "- / + edit a draft. Tap SetTime (below) to write it "
                            "to the RTC; the change survives a reboot.");
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);

    /* Commit row: lives at the bottom-right of THIS card (not the page footer,
     * per the requested layout).  - / + only edit the draft; this is the single
     * control that writes it to the RTC. */
    lv_obj_t *commit = lv_obj_create(card);
    lv_obj_remove_style_all(commit);
    lv_obj_set_width(commit, LV_PCT(100));
    lv_obj_set_height(commit, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(commit, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(commit, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(commit, Theme::kGapSm, 0);
    lv_obj_clear_flag(commit, LV_OBJ_FLAG_SCROLLABLE);
    ui::app_button(commit, "SetTime", settime_cb, this);

    return card;
}

void SettingsPage::refresh_datetime()
{
    /* Render the draft, not the RTC - the user is mid-edit until SetTime. */
    const int vals[6] = {draft_.year, draft_.month, draft_.day,
                          draft_.hour, draft_.minute, draft_.second};
    char buf[12];
    for (int f = 0; f < 6; ++f) {
        if (datetime_val_[f] == nullptr) {
            continue;
        }
        snprintf(buf, sizeof(buf), "%d", vals[f]);
        lv_label_set_text(datetime_val_[f], buf);
    }
}

void SettingsPage::datetime_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (self == nullptr || btn == nullptr) {
        return;
    }

    const intptr_t tag = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));
    const DtField field = static_cast<DtField>(tag >> 1);
    const int delta = (tag & 1) ? 1 : -1;

    /* Edit the draft only.  The RTC is not touched here. */
    switch (field) {
    case DtField::Year:   self->draft_.year   = clamp(self->draft_.year + delta, 2000, 2099); break;
    case DtField::Month:  self->draft_.month  = wrap(self->draft_.month + delta, 1, 12);       break;
    case DtField::Day: {
        const int max = services::clock_days_in_month(self->draft_.year, self->draft_.month);
        self->draft_.day = clamp(self->draft_.day + delta, 1, max);
        break;
    }
    case DtField::Hour:   self->draft_.hour   = wrap(self->draft_.hour + delta, 0, 23);        break;
    case DtField::Minute: self->draft_.minute = wrap(self->draft_.minute + delta, 0, 59);      break;
    case DtField::Second: self->draft_.second = wrap(self->draft_.second + delta, 0, 59);      break;
    }

    /* A new year or month can make the stored day illegal; clamp it so the
     * committed value is always a date the RTC will accept. */
    const int max_day = services::clock_days_in_month(self->draft_.year, self->draft_.month);
    if (self->draft_.day > max_day) {
        self->draft_.day = max_day;
    }

    self->refresh_datetime();
}

void SettingsPage::settime_cb(lv_event_t *e)
{
    SettingsPage *self = static_cast<SettingsPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }

    /* The draft's year is already clamped to 2000-2099 by datetime_cb, which
     * matches the PCF85063A's 0-99 year register exactly.  Only the day can
     * still be out of range after a month/year change, so clamp that and write. */
    services::TimeParts t = self->draft_;
    const int max_day = services::clock_days_in_month(t.year, t.month);
    if (t.day > max_day) {
        t.day = max_day;
    }

    const esp_err_t err = services::clock_set(&t);
    if (err == ESP_OK) {
        self->notify("time set");
    } else {
        self->notify("set failed");
        ESP_LOGW(TAG, "cannot set the RTC: %s", esp_err_to_name(err));
    }
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
    if (poll_timer_ != nullptr) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        /* The overlay and its keyboard are children of root_, so deleting the
         * root takes them; the pointers still have to be cleared or a stale
         * one would be dereferenced by the next instance's poll tick. */
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    brightness_row_ = nullptr;
    footer_note_ = nullptr;
    wifi_overlay_ = nullptr;
    wifi_body_ = nullptr;
    wifi_summary_ = nullptr;
    wifi_password_ = nullptr;
    for (int i = 0; i < 4; ++i) {
        net_rows_[i] = nullptr;
    }
    for (int i = 0; i < 3; ++i) {
        storage_rows_[i] = nullptr;
    }
}

void SettingsPage::on_enter()
{
    /* Start the draft at whatever the RTC currently holds, so a fresh open
     * always reflects the real time rather than a stale edit. */
    services::clock_now(&draft_);

    refresh();
    /* 400 ms: fast enough that a connection appears to happen when the button
     * is pressed, slow enough that the cost is invisible.  Association takes
     * seconds, so nothing is gained by polling harder. */
    if (poll_timer_ == nullptr) {
        poll_timer_ = lv_timer_create(net_poll_tick, 400, this);
    }
    ESP_LOGI(TAG, "network state: %s",
             services::net_wifi_state_text(services::net_wifi_state()));
}

void SettingsPage::on_leave()
{
    if (note_timer_ != nullptr) {
        lv_timer_delete(note_timer_);
        note_timer_ = nullptr;
    }
    /* Leaving stops the polling: there is nothing on screen to update, and a
     * timer that outlives the page's visibility is how a stale label gets
     * written into a page that is about to be destroyed. */
    if (poll_timer_ != nullptr) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void SettingsPage::refresh()
{
    refresh_display();
    refresh_network();
    refresh_storage();
    refresh_datetime();
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

    if (strcmp(label, "WiFi setup") == 0) {
        /* The overlay owns the scan/credential flow; this card stays the
         * status display plus the two actions that make sense without one. */
        self->open_wifi_overlay();
        return;
    }

    if (strcmp(label, "Connect") == 0) {
        const esp_err_t err = services::net_wifi_reconnect();
        if (err == ESP_OK) {
            self->notify("connecting...");
        } else if (err == ESP_ERR_NOT_FOUND) {
            self->notify("no network configured - use WiFi setup");
        } else {
            self->notify("cannot connect");
        }
    } else if (strcmp(label, "Disconnect") == 0) {
        const esp_err_t err = services::net_wifi_disconnect();
        self->notify(err == ESP_OK ? "disconnect requested" : "nothing to disconnect");
    } else if (strcmp(label, "Forget") == 0) {
        const esp_err_t err = services::net_wifi_forget();
        self->notify(err == ESP_OK ? "credentials erased" : "erase failed");
    }
    self->refresh_network();
}

void SettingsPage::restart_cb(lv_event_t *)
{
    ESP_LOGW(TAG, "restart requested from Settings");
    app::AppManager::instance().request_restart();
}
