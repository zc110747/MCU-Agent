/**
 * @file home_page.cpp
 * @brief The root page: nine application tiles on a 3x3 grid.
 *
 * Layout notes for the fixed 800x480 landscape panel
 * ---------------------------------------------------
 * The body is a Grid with three flex-fraction columns and three rows, so the
 * tiles divide whatever space the header and footer leave.  Nothing here
 * scales the reference design: the tile count happens to be nine, which fits a
 * 3x3 grid exactly, and the grid (not a pixel constant) is what decides the
 * tile size.  Add a tenth tile and it wraps to a fourth row and the grid
 * shrinks all of them -- there is no width or height written down twice.
 *
 * Each tile is a row of [48 px glyph][title / subtitle].  The glyphs are the
 * generated ARGB8888 set, tinted to the accent colour at draw time.
 */

#include "home_page.h"

#include "app_icons.h"
#include "app_manager.h"
#include "system_info.h"
#include "theme.h"
#include "widgets.h"

#include "esp_log.h"

static const char *TAG = "home";

namespace {

struct AppTile {
    const lv_image_dsc_t *icon;
    const char           *title;
    const char           *subtitle;
    app::PageId           id;
};

/* Order matters: it is the order they are placed into the grid, row by row. */
const AppTile kApps[] = {
    {&icon_reader,   "Reader",   "/sd/Ebook/txt", app::PageId::Reader},
    {&icon_photos,   "Photos",   "JPG / PNG",     app::PageId::Photos},
    {&icon_notes,    "Notes",    "plain text",    app::PageId::Notes},
    {&icon_weather,  "Weather",  "online",        app::PageId::Weather},
    {&icon_clock,    "Clock",    "RTC",           app::PageId::Clock},
    {&icon_calendar, "Calendar", "RTC",           app::PageId::Calendar},
    {&icon_drawing,  "Drawing",  "canvas",        app::PageId::Drawing},
    {&icon_files,    "Files",    "/sd",           app::PageId::FileManager},
    {&icon_settings, "Settings", "system",        app::PageId::Settings},
};

constexpr int kCols = 3;
constexpr int kRows = 3;

lv_obj_t *make_tile(lv_obj_t *parent, const AppTile &app)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_remove_style_all(tile);
    lv_obj_add_style(tile, Theme::tile(), 0);
    lv_obj_add_style(tile, Theme::tile_pressed(), LV_STATE_PRESSED);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tile, Theme::kGapMd, 0);

    lv_obj_t *img = lv_image_create(tile);
    lv_image_set_src(img, app.icon);
    lv_obj_set_style_image_recolor(img, lv_color_hex(Theme::kAccent), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);

    lv_obj_t *texts = lv_obj_create(tile);
    lv_obj_remove_style_all(texts);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(texts, 2, 0);
    lv_obj_clear_flag(texts, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(texts);
    lv_obj_add_style(title, Theme::text_title(), 0);
    lv_label_set_text(title, app.title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, LV_PCT(100));

    lv_obj_t *sub = lv_label_create(texts);
    lv_obj_add_style(sub, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(sub, Theme::font_small(), 0);
    lv_label_set_text(sub, app.subtitle);
    lv_obj_set_width(sub, LV_PCT(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_add_event_cb(tile, HomePage::tile_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(app.id)));
    return tile;
}

}  // namespace

void HomePage::create(lv_obj_t *parent)
{
    /* with_close = false: Home is the application list itself, and the shell
     * it launched from.  An exit button here would be a control that does
     * nothing - every other page gets one. */
    ui::PageLayout page = ui::page_layout(parent, "Ebook", true, false);
    root_ = page.root;

    /* Header right: live memory readout, refreshed by status_tick(). */
    status_label_ = lv_label_create(page.header_right);
    lv_obj_add_style(status_label_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(status_label_, Theme::font_small(), 0);
    lv_label_set_text(status_label_, "SRAM --");

    /* Body: 3x3 grid.  The row/col descriptors must outlive the call because
     * LVGL keeps the pointer, hence the function-local statics.            */
    static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                      LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                      LV_GRID_TEMPLATE_LAST};

    lv_obj_t *body = page.body;
    lv_obj_set_grid_dsc_array(body, col_dsc, row_dsc);
    lv_obj_set_layout(body, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(body, Theme::kGapMd, 0);
    lv_obj_set_style_pad_column(body, Theme::kGapMd, 0);
    /* The grid must fit the viewport exactly; scrolling is for pages whose
     * content can grow without bound, and this one cannot.                 */
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < sizeof(kApps) / sizeof(kApps[0]); ++i) {
        lv_obj_t *tile = make_tile(body, kApps[i]);
        lv_obj_set_grid_cell(tile,
                             LV_GRID_ALIGN_STRETCH, (int32_t)(i % kCols), 1,
                             LV_GRID_ALIGN_STRETCH, (int32_t)(i / kCols), 1);
    }

    /* Footer: the two hardware acceptance pages are reachable from here so
     * they can be exercised without a working SD card or network.          */
    lv_obj_t *hint = lv_label_create(page.footer_left);
    lv_obj_add_style(hint, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(hint, Theme::font_small(), 0);
    lv_label_set_text(hint, "8 apps + settings");

    ui::app_button(page.footer_right, "Display", diag_cb,
                   reinterpret_cast<void *>(static_cast<uintptr_t>(app::PageId::DisplayTest)));
    ui::app_button(page.footer_right, "Touch", diag_cb,
                   reinterpret_cast<void *>(static_cast<uintptr_t>(app::PageId::TouchTest)));

    /* app_button() is sized for a 48 px touch target, which does not fit the
     * 36 px footer bar.  These two are deliberately below the minimum: they
     * are developer entry points, not part of the user-facing flow, and the
     * real ones will live in Settings once that page exists.               */
    const uint32_t n = lv_obj_get_child_count(page.footer_right);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t *btn = lv_obj_get_child(page.footer_right, (int32_t)i);
        lv_obj_set_height(btn, 32);
        lv_obj_set_style_pad_ver(btn, 4, 0);
    }

    ESP_LOGI(TAG, "home built with %u tiles on a %dx%d grid",
             (unsigned)(sizeof(kApps) / sizeof(kApps[0])), kCols, kRows);
}

void HomePage::destroy()
{
    if (status_timer_ != nullptr) {
        lv_timer_delete(status_timer_);
        status_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    status_label_ = nullptr;
}

void HomePage::on_enter()
{
    refresh_status();
    status_timer_ = lv_timer_create(status_tick, 2000, this);
}

void HomePage::on_leave()
{
    if (status_timer_ != nullptr) {
        lv_timer_delete(status_timer_);
        status_timer_ = nullptr;
    }
}

void HomePage::tile_cb(lv_event_t *e)
{
    const app::PageId id = static_cast<app::PageId>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    app::navigate(id);
}

void HomePage::diag_cb(lv_event_t *e)
{
    tile_cb(e);
}

void HomePage::status_tick(lv_timer_t *timer)
{
    /* Reached from a timer owned by this page, so the user data is the page.
     * (An earlier draft asked AppManager for the current page and cast it,
     * which would silently misbehave the moment the page changed.)         */
    HomePage *self = static_cast<HomePage *>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->refresh_status();
    }
}

void HomePage::refresh_status()
{
    if (status_label_ == nullptr) {
        return;
    }

    system_info_t info;
    system_info_collect(&info);
    lv_label_set_text_fmt(status_label_, "SRAM %uK   PSRAM %uK",
                          (unsigned)(info.heap_internal_free / 1024),
                          (unsigned)(info.heap_psram_free / 1024));
}
