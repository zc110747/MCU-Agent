/**
 * @file display_test_page.cpp
 * @brief Phase 1 acceptance screen implementation.
 *
 * The page is built for a fixed 800x480 landscape panel.  There is no scale
 * factor anywhere: every size below is a deliberate real-pixel decision, and
 * the two cards share the free width by flex weight (44 : 56) rather than by
 * hard-coded widths, so nothing can silently squash when a card changes.
 */

#include "display_test_page.h"

#include "board_config.h"
#include "display_driver.h"
#include "test_pattern.h"
#include "theme.h"

#include "esp_log.h"

static const char *TAG = "display_test";

/* --- test signal colours -------------------------------------------------
 * Deliberately NOT theme colours.  These are measurement references: red,
 * green and blue must come out as red, green and blue, otherwise the panel
 * wiring or the RGB565 bit mapping is wrong.  Kept out of Theme on purpose so
 * nobody "unifies" them later. */
#define TEST_RED    0xF800
#define TEST_GREEN  0x07E0
#define TEST_BLUE   0x001F
#define TEST_WHITE  0xFFFF
#define TEST_BLACK  0x000000

/* Straight-line test geometry: a flat 3 px blue line, long enough that a
 * partial-row or doubled-row artefact cannot hide inside it. */
static lv_point_precise_t s_line_points[2] = {{0, 2}, {160, 2}};

void DisplayTestPage::create(lv_obj_t *parent)
{
    /* ---- root: header / body / footer, vertical flex ------------------ */
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_add_style(root_, Theme::screen(), 0);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_style_pad_row(root_, 0, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- header ------------------------------------------------------- */
    lv_obj_t *header = lv_obj_create(root_);
    lv_obj_remove_style_all(header);
    lv_obj_add_style(header, Theme::header(), 0);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, Theme::kHeaderH);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_obj_add_style(title, Theme::text_title(), 0);
    lv_label_set_text(title, "Ebook LVGL");

    lv_obj_t *geometry = lv_label_create(header);
    lv_obj_add_style(geometry, Theme::text_dim(), 0);
    lv_label_set_text_fmt(geometry, "%d x %d  landscape  RGB565",
                          BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    /* ---- body --------------------------------------------------------- */
    lv_obj_t *body = lv_obj_create(root_);
    lv_obj_remove_style_all(body);                 /* shows the screen bg */
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(body, Theme::kSafePad, 0);
    lv_obj_set_style_pad_column(body, Theme::kGapLg, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    make_shape_card(body);
    make_image_card(body);

    /* ---- footer ------------------------------------------------------- */
    lv_obj_t *footer = lv_obj_create(root_);
    lv_obj_remove_style_all(footer);
    lv_obj_add_style(footer, Theme::footer(), 0);
    lv_obj_set_width(footer, LV_PCT(100));
    lv_obj_set_height(footer, Theme::kFooterH);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *board = lv_label_create(footer);
    lv_obj_add_style(board, Theme::text_dim(), 0);
    lv_label_set_text(board, BOARD_NAME);

    uptime_label_ = lv_label_create(footer);
    lv_obj_add_style(uptime_label_, Theme::text_dim(), 0);
    lv_label_set_text_fmt(uptime_label_, "LCD OK   LVGL %d.%d.%d   up 0 s",
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    ESP_LOGI(TAG, "test page built on %s", BOARD_NAME);
}

lv_obj_t *DisplayTestPage::make_card(lv_obj_t *parent, const char *title_text)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, Theme::card(), 0);
    lv_obj_set_width(card, 0);                     /* width comes from grow */
    lv_obj_set_height(card, LV_PCT(100));
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, Theme::kGapMd, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(card);
    lv_obj_add_style(label, Theme::text_title(), 0);
    lv_label_set_text(label, title_text);

    return card;
}

lv_obj_t *DisplayTestPage::make_chip(lv_obj_t *parent, uint32_t rgb,
                                     const char *text, uint32_t text_rgb)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 56, 30);
    lv_obj_set_style_bg_color(chip, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 4, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(Theme::kBorder), 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(chip);
    lv_obj_set_style_text_font(label, Theme::font_small(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(text_rgb), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return chip;
}

lv_obj_t *DisplayTestPage::make_shape_card(lv_obj_t *parent)
{
    lv_obj_t *card = make_card(parent, "Shapes / text");
    lv_obj_set_flex_grow(card, 44);

    /* --- rectangle, circle and line side by side ---------------------- */
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 96);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, Theme::kGapLg, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *rect = lv_obj_create(row);
    lv_obj_remove_style_all(rect);
    lv_obj_set_size(rect, 120, 80);                /* must render 120x80  */
    lv_obj_set_style_bg_color(rect, lv_color_hex(TEST_RED), 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rect, 0, 0);
    lv_obj_clear_flag(rect, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *circle = lv_obj_create(row);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 80, 80);               /* must render as a disc */
    lv_obj_set_style_bg_color(circle, lv_color_hex(TEST_GREEN), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *line = lv_line_create(row);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, 160, 5);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_line_set_points(line, s_line_points, 2);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(TEST_BLUE), 0);
    lv_obj_set_style_line_rounded(line, false, 0);

    /* --- 1 px divider: proves one screen row is exactly one row -------- */
    lv_obj_t *divider = lv_obj_create(card);
    lv_obj_remove_style_all(divider);
    lv_obj_add_style(divider, Theme::divider(), 0);
    lv_obj_set_width(divider, LV_PCT(100));
    lv_obj_set_height(divider, 1);

    /* --- text: sizes must not be stretched ---------------------------- */
    lv_obj_t *text = lv_label_create(card);
    lv_obj_add_style(text, Theme::text_body(), 0);
    lv_label_set_text(text, "Text 0123456789 ABCDEFGHIJ abcdefghij");

    lv_obj_t *text_dim = lv_label_create(card);
    lv_obj_add_style(text_dim, Theme::text_dim(), 0);
    lv_label_set_text(text_dim, "120x80 rect / 80x80 circle / 3 px line");

    return card;
}

lv_obj_t *DisplayTestPage::make_image_card(lv_obj_t *parent)
{
    lv_obj_t *card = make_card(parent, "Image / aspect ratio");
    lv_obj_set_flex_grow(card, 56);

    /* 96x72 RGB565 test pattern: 8 colour bars on top, a black separator,
     * then a 24x24 white square that must look square. */
    lv_obj_t *image = lv_image_create(card);
    lv_image_set_src(image, &test_pattern);

    lv_obj_t *caption = lv_label_create(card);
    lv_obj_add_style(caption, Theme::text_dim(), 0);
    lv_label_set_text_fmt(caption, "96 x 72 px pattern, 4:3, %u bytes",
                          (unsigned)test_pattern.data_size);

    /* --- colour chips -------------------------------------------------- */
    lv_obj_t *chips = lv_obj_create(card);
    lv_obj_remove_style_all(chips);
    lv_obj_set_width(chips, LV_PCT(100));
    lv_obj_set_height(chips, 34);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chips, Theme::kGapSm, 0);
    lv_obj_clear_flag(chips, LV_OBJ_FLAG_SCROLLABLE);

    make_chip(chips, TEST_RED,   "R", TEST_WHITE);
    make_chip(chips, TEST_GREEN, "G", TEST_BLACK);
    make_chip(chips, TEST_BLUE,  "B", TEST_WHITE);
    make_chip(chips, TEST_WHITE, "W", TEST_BLACK);
    make_chip(chips, TEST_BLACK, "K", TEST_WHITE);

    lv_obj_t *note = lv_label_create(card);
    lv_obj_add_style(note, Theme::text_dim(), 0);
    lv_label_set_text(note, "square must look square, circle round");

    return card;
}

void DisplayTestPage::on_enter()
{
    seconds_ = 0;
    uptime_timer_ = lv_timer_create(uptime_cb, 1000, this);
    ESP_LOGD(TAG, "entered");
}

void DisplayTestPage::on_leave()
{
    if (uptime_timer_ != nullptr) {
        lv_timer_delete(uptime_timer_);
        uptime_timer_ = nullptr;
    }
    ESP_LOGD(TAG, "left");
}

void DisplayTestPage::destroy()
{
    /* on_leave() normally already killed the timer; this is belt and braces
     * for the case where destroy() is reached some other way. */
    if (uptime_timer_ != nullptr) {
        lv_timer_delete(uptime_timer_);
        uptime_timer_ = nullptr;
    }

    if (root_ != nullptr) {
        lv_obj_delete(root_);      /* frees the whole child tree */
        root_ = nullptr;
    }
    uptime_label_ = nullptr;
}

void DisplayTestPage::uptime_cb(lv_timer_t *timer)
{
    DisplayTestPage *self = static_cast<DisplayTestPage *>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->uptime_label_ == nullptr) {
        return;
    }

    ++self->seconds_;
    /* This label only advances if the LVGL task keeps running and the frame
     * keeps reaching the panel - it is the cheapest possible "is it alive"
     * probe for a 30 minute soak. */
    lv_label_set_text_fmt(self->uptime_label_, "LCD OK   LVGL %d.%d.%d   up %u s",
                          LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                          (unsigned)self->seconds_);

    /* Every 5 s, read the panel frame buffers back and report what is in them.
     * This is the only acceptance evidence that does not need a human looking
     * at the screen: LVGL draws straight into these buffers, so their content
     * is simultaneously (a) proof the render pipeline reached the glass and
     * (b) proof the RGB565 byte order matches the panel wiring.
     *
     * "drawn" is the count of non-zero pixels.  The theme background is very
     * dark (0x0882) but not black, so a correctly painted screen reports
     * drawn == sampled, while an untouched buffer reports 0.               */
    if ((self->seconds_ % 5u) == 0u) {
        display_fb_report_t rep = {};
        rep.bg_rgb565      = lv_color_to_u16(lv_color_hex(Theme::kBg));
        rep.surface_rgb565 = lv_color_to_u16(lv_color_hex(Theme::kSurface));
        if (display_fb_report(&rep) == ESP_OK) {
            for (int i = 0; i < BOARD_LCD_NUM_FB; ++i) {
                const display_fb_census_t *c = &rep.fb[i];
                ESP_LOGI(TAG,
                         "fb[%d] present=%d drawn=%u/%u corner=%04X,%04X,%04X,%04X "
                         "centre=%04X | theme bg=%04X surface=%04X",
                         i, (int)c->present, (unsigned)c->non_zero, (unsigned)c->sampled,
                         c->corner[0], c->corner[1], c->corner[2], c->corner[3],
                         c->centre, rep.bg_rgb565, rep.surface_rgb565);
            }
        } else {
            ESP_LOGW(TAG, "frame buffer read-back failed");
        }
    }
}
