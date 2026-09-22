/**
 * @file drawing_page.cpp
 * @brief Phase 9: a touch canvas that can be cleared and saved as a BMP.
 *
 * WHY A CANVAS AND NOT A PILE OF LV_LINE OBJECTS
 * ----------------------------------------------
 * A stroke is a few hundred points; one lv_line per segment would mean
 * thousands of objects, each with its own style, all re-evaluated during layout
 * on every frame.  A canvas is one object with one pixel buffer: drawing costs
 * exactly the pixels touched, and the whole buffer is composited in a single
 * pass.  It also gives an exact, inspectable artefact - which is what makes
 * "finger at A, ink at B" testable rather than a matter of opinion, because the
 * saved BMP contains the coordinates the finger actually produced.
 *
 * COORDINATE MAPPING
 * ------------------
 * The pointer position arrives as a screen coordinate and is converted once,
 * against the canvas's own geometry, by subtracting the canvas origin.  There is
 * no scaling factor anywhere: the canvas is 1:1 with the panel, so the ink lands
 * on the pixel the finger is on.  If that ever stops being true, the offset is
 * visible in the saved file.
 */

#include "drawing_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_icons.h"
#include "app_manager.h"
#include "clock_service.h"
#include "storage_service.h"
#include "theme.h"
#include "widgets.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "drawing";

namespace {

/* Panel layout arithmetic for the 800x480 page, spelled out so that a change to
 * any of the three inputs is obviously a change to the canvas size:
 *   480 total - 56 header - 36 footer = 388 body
 *   388 - 2 * 16 body padding         = 356 usable
 *   356 - 52 toolbar - 16 gap          = 288 canvas height
 * Width is the full usable width: 800 - 2 * 16 = 768. */
constexpr int32_t kToolbarH = 52;
constexpr int32_t kCanvasW = 800 - 2 * Theme::kSafePad;
constexpr int32_t kCanvasH = 288;

/* Palette.  Kept as a table rather than five hand-built buttons so the pen
 * colour and the swatch that selected it cannot disagree. */
struct Swatch {
    uint32_t    rgb;
    const char *name;
};
const Swatch kSwatches[] = {
    {0xFFFFFF, "white"},
    {0x4C8DFF, "blue"},
    {0xE5534B, "red"},
    {0x3FB950, "green"},
    {0xE3B341, "yellow"},
};
constexpr int kSwatchCount = (int)(sizeof(kSwatches) / sizeof(kSwatches[0]));

constexpr int32_t kWidths[] = {3, 6, 12};
constexpr int kWidthCount = (int)(sizeof(kWidths) / sizeof(kWidths[0]));
constexpr int32_t kToolbarBtnW = 40;

/** @brief Build a colour swatch that knows its page. */
lv_obj_t *make_swatch(lv_obj_t *parent, int index, DrawingPage *page, lv_color_t color)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, Theme::icon_btn(), 0);
    lv_obj_add_style(btn, Theme::icon_btn_pressed(), LV_STATE_PRESSED);
    lv_obj_set_size(btn, kToolbarBtnW, kToolbarBtnW);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

    lv_obj_add_event_cb(btn, DrawingPage::pen_cb, LV_EVENT_CLICKED, page);
    /* Index in the object slot, page in the event slot - see clock_page.cpp. */
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    return btn;
}

}  // namespace

/* ------------------------------------------------------------------------ */
/* build                                                                    */
/* ------------------------------------------------------------------------ */

void DrawingPage::create(lv_obj_t *parent)
{
    ui::PageLayout page = ui::page_layout(parent, "Drawing", true);
    root_ = page.root;

    ui::app_button(page.header_right, "Clear", clear_cb, this);
    ui::app_button(page.header_right, "Save", save_cb, this);

    lv_obj_t *body = page.body;
    lv_obj_set_style_pad_row(body, Theme::kGapMd, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

    /* ---- toolbar ------------------------------------------------------- */
    lv_obj_t *bar = lv_obj_create(body);
    lv_obj_remove_style_all(bar);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, kToolbarH);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, Theme::kGapSm, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pen_label = lv_label_create(bar);
    lv_obj_add_style(pen_label, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(pen_label, Theme::font_small(), 0);
    lv_label_set_text(pen_label, "Pen");

    for (int i = 0; i < kSwatchCount; ++i) {
        swatches_[i] = make_swatch(bar, i, this, lv_color_hex(kSwatches[i].rgb));
    }

    lv_obj_t *sep = lv_obj_create(bar);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 1, kToolbarH - 16);
    lv_obj_set_style_bg_color(sep, lv_color_hex(Theme::kBorder), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

    lv_obj_t *width_label = lv_label_create(bar);
    lv_obj_add_style(width_label, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(width_label, Theme::font_small(), 0);
    lv_label_set_text(width_label, "Size");

    for (int i = 0; i < kWidthCount; ++i) {
        /* Width buttons share pen_cb; they are told apart by an index that is
         * offset past the swatches. */
        lv_obj_t *btn = make_swatch(bar, kSwatchCount + i, this,
                                    lv_color_hex(Theme::kSurface2));
        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_style_text_font(label, Theme::font_small(), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(Theme::kText), 0);
        lv_label_set_text_fmt(label, "%d", (int)kWidths[i]);
        lv_obj_center(label);
        widths_[i] = btn;
    }

    /* ---- canvas -------------------------------------------------------- */
    canvas_ = lv_canvas_create(body);
    canvas_w_ = kCanvasW;
    canvas_h_ = kCanvasH;

    /* The buffer must outlive the canvas and be sized with LVGL's own macro:
     * the stride is rounded up for alignment, so w*h*2 would be short and the
     * last row would write past the end. */
    const size_t buf_size = LV_DRAW_BUF_SIZE(kCanvasW, kCanvasH, LV_COLOR_FORMAT_RGB565);
    pixels_ = malloc(buf_size);
    if (pixels_ == nullptr) {
        ESP_LOGE(TAG, "cannot allocate a %ux%u canvas (%u bytes)",
                 (unsigned)kCanvasW, (unsigned)kCanvasH, (unsigned)buf_size);
        /* A canvas with no buffer is not a canvas: drop the widget rather than
         * leave one that would draw from a null pointer. */
        lv_obj_delete(canvas_);
        canvas_ = nullptr;
        ui::empty_state(body, "--", "Not enough memory",
                        "The drawing canvas could not be allocated. Close other pages and try again.");
        ui::app_button(page.footer_right, "Back", back_cb, nullptr);
        return;
    }

    /* RGB565 rather than ARGB8888: strokes are opaque, so the alpha channel
     * would only double the buffer (and the per-frame compositing cost) to
     * carry information nothing reads. */
    lv_canvas_set_buffer(canvas_, pixels_, kCanvasW, kCanvasH, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas_, kCanvasW, kCanvasH);
    lv_obj_clear_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(canvas_, Theme::kRadiusSm, 0);
    lv_obj_set_style_border_width(canvas_, 1, 0);
    lv_obj_set_style_border_color(canvas_, lv_color_hex(Theme::kBorder), 0);
    lv_obj_add_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(canvas_, canvas_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(canvas_, canvas_cb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(canvas_, canvas_cb, LV_EVENT_RELEASED, this);

    /* Default pen: white, medium. Set after the swatches exist so the initial
     * selection highlight matches the colour actually in use. */
    pen_color_ = lv_color_hex(kSwatches[0].rgb);
    pen_width_ = kWidths[1];
    lv_obj_set_style_border_color(swatches_[0], lv_color_hex(Theme::kAccent), 0);
    lv_obj_set_style_border_width(swatches_[0], 2, 0);
    lv_obj_set_style_border_color(widths_[1], lv_color_hex(Theme::kAccent), 0);
    lv_obj_set_style_border_width(widths_[1], 2, 0);

    /* ---- footer -------------------------------------------------------- */
    footer_note_ = lv_label_create(page.footer_left);
    lv_obj_add_style(footer_note_, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(footer_note_, Theme::font_small(), 0);
    lv_label_set_text_fmt(footer_note_, "canvas %dx%d, 1:1 with the panel",
                          (int)kCanvasW, (int)kCanvasH);

    ui::app_button(page.footer_right, "Back", back_cb, nullptr);

    clear();
    ESP_LOGI(TAG, "drawing built: canvas %dx%d, buffer %u bytes",
             (int)kCanvasW, (int)kCanvasH, (unsigned)buf_size);
}

void DrawingPage::destroy()
{
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    /* The canvas keeps a pointer to this buffer, so it is released only after
     * the widget that references it is gone. */
    free(pixels_);
    pixels_ = nullptr;
    canvas_ = nullptr;
    footer_note_ = nullptr;
    have_last_ = false;
    for (int i = 0; i < 5; ++i) {
        swatches_[i] = nullptr;
    }
    for (int i = 0; i < 3; ++i) {
        widths_[i] = nullptr;
    }
}

void DrawingPage::on_enter()
{
    have_last_ = false;
}

/* ------------------------------------------------------------------------ */
/* drawing                                                                  */
/* ------------------------------------------------------------------------ */

void DrawingPage::clear()
{
    if (canvas_ == nullptr) {
        return;
    }
    lv_canvas_fill_bg(canvas_, lv_color_hex(Theme::kBg), LV_OPA_COVER);
    lv_obj_invalidate(canvas_);
}

void DrawingPage::stroke(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    if (canvas_ == nullptr) {
        return;
    }

    /* lv_canvas_init_layer() gives a draw context bound to the canvas buffer;
     * drawing through it is what produces an anti-aliased, round-capped line
     * instead of the staircase you get from setting pixels by hand. */
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = pen_color_;
    dsc.width = pen_width_;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = x0;
    dsc.p1.y = y0;
    dsc.p2.x = x1;
    dsc.p2.y = y1;

    lv_draw_line(&layer, &dsc);
    lv_canvas_finish_layer(canvas_, &layer);

    /* Finish-layer already invalidates the changed area, but the canvas is
     * composited as a whole image, so ask for the whole widget once. */
    lv_obj_invalidate(canvas_);
}

void DrawingPage::canvas_cb(lv_event_t *e)
{
    DrawingPage *self = static_cast<DrawingPage *>(lv_event_get_user_data(e));
    if (self == nullptr || self->canvas_ == nullptr) {
        return;
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == nullptr) {
        return;   /* synthetic event: no pointer to read */
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    /* Screen coordinate -> canvas pixel.  This subtraction is the entire
     * calibration: if it is wrong, the saved BMP shows it. */
    lv_area_t area;
    lv_obj_get_coords(self->canvas_, &area);
    int32_t x = p.x - area.x1;
    int32_t y = p.y - area.y1;

    /* Clamp rather than discard: a finger that leaves the canvas and comes back
     * should resume drawing at the edge, not start a new stroke. */
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > self->canvas_w_ - 1) x = self->canvas_w_ - 1;
    if (y > self->canvas_h_ - 1) y = self->canvas_h_ - 1;

    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        /* A tap with no movement still has to leave a mark, so the first event
         * draws a zero-length line - which the round caps turn into a dot. */
        self->last_.x = x;
        self->last_.y = y;
        self->have_last_ = true;
        self->stroke(x, y, x, y);
        break;

    case LV_EVENT_PRESSING:
        if (!self->have_last_) {
            self->last_.x = x;
            self->last_.y = y;
            self->have_last_ = true;
            self->stroke(x, y, x, y);
            break;
        }
        /* LVGL reports PRESSING every refresh period, so on a fast flick the
         * segments are long.  Joining each point to the previous one keeps the
         * stroke continuous; a spline would smooth it but is not needed to make
         * the ink follow the finger. */
        self->stroke(self->last_.x, self->last_.y, x, y);
        self->last_.x = x;
        self->last_.y = y;
        break;

    case LV_EVENT_RELEASED:
        if (self->have_last_ &&
            (self->last_.x != x || self->last_.y != y)) {
            self->stroke(self->last_.x, self->last_.y, x, y);
        }
        self->have_last_ = false;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* saving                                                                   */
/* ------------------------------------------------------------------------ */

namespace {

void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

}  // namespace

void DrawingPage::save()
{
    if (pixels_ == nullptr || canvas_ == nullptr || footer_note_ == nullptr) {
        return;
    }
    if (!services::storage_ready()) {
        lv_label_set_text(footer_note_, "no SD card: cannot save");
        return;
    }

    services::storage_mkdir("/sd/notes");

    services::TimeParts t;
    services::clock_now(&t);
    char path[256];
    snprintf(path, sizeof(path), "/sd/notes/draw_%04d%02d%02d_%02d%02d%02d.bmp",
             t.year, t.month, t.day, t.hour, t.minute, t.second);

    /* A 24-bit BMP is written because it is the smallest format that loses
     * nothing here and needs no compressor: a header, then bottom-up rows of
     * BGR triples padded to a 4-byte boundary.  LVGL's own BMP decoder can read
     * it back, which is how the result can be checked on the device itself. */
    const uint32_t row_raw = (uint32_t)canvas_w_ * 3u;
    const uint32_t row_padded = (row_raw + 3u) & ~3u;
    const uint32_t pixel_bytes = row_padded * (uint32_t)canvas_h_;
    const uint32_t file_size = 54u + pixel_bytes;

    uint8_t *file = static_cast<uint8_t *>(malloc(file_size));
    if (file == nullptr) {
        lv_label_set_text(footer_note_, "save failed: out of memory");
        ESP_LOGE(TAG, "cannot allocate %u bytes for the BMP", (unsigned)file_size);
        return;
    }

    /* --- BITMAPFILEHEADER (14 bytes) --- */
    file[0] = 'B';
    file[1] = 'M';
    put_le32(file + 2, file_size);
    put_le16(file + 6, 0);
    put_le16(file + 8, 0);
    put_le32(file + 10, 54);

    /* --- BITMAPINFOHEADER (40 bytes) --- */
    put_le32(file + 14, 40);
    put_le32(file + 18, (uint32_t)canvas_w_);
    /* Positive height means rows are stored bottom-up. */
    put_le32(file + 22, (uint32_t)canvas_h_);
    put_le16(file + 26, 1);      /* planes          */
    put_le16(file + 28, 24);     /* bits per pixel  */
    put_le32(file + 30, 0);      /* BI_RGB, no compression */
    put_le32(file + 34, pixel_bytes);
    put_le32(file + 38, 2835);   /* 72 dpi */
    put_le32(file + 42, 2835);
    put_le32(file + 46, 0);
    put_le32(file + 50, 0);

    const uint16_t *src = static_cast<const uint16_t *>(pixels_);
    const uint32_t stride_px = (uint32_t)(LV_DRAW_BUF_STRIDE(canvas_w_, LV_COLOR_FORMAT_RGB565) / 2);

    for (int32_t y = 0; y < canvas_h_; ++y) {
        /* BMP rows run bottom-up. */
        const uint16_t *row = src + (size_t)(canvas_h_ - 1 - y) * stride_px;
        uint8_t *dst = file + 54u + (size_t)y * row_padded;

        for (int32_t x = 0; x < canvas_w_; ++x) {
            const uint16_t c = row[x];
            /* RGB565 -> 8 bit per channel, expanded by bit replication so that
             * full scale maps to 255 rather than 248. */
            const uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
            const uint8_t g6 = (uint8_t)((c >> 5) & 0x3F);
            const uint8_t b5 = (uint8_t)(c & 0x1F);
            const uint8_t r = (uint8_t)((r5 << 3) | (r5 >> 2));
            const uint8_t g = (uint8_t)((g6 << 2) | (g6 >> 4));
            const uint8_t b = (uint8_t)((b5 << 3) | (b5 >> 2));
            dst[x * 3 + 0] = b;
            dst[x * 3 + 1] = g;
            dst[x * 3 + 2] = r;
        }
        /* Padding bytes must be zero; memset is cheaper than a per-byte loop. */
        for (uint32_t i = row_raw; i < row_padded; ++i) {
            dst[i] = 0;
        }
    }

    const esp_err_t err = services::storage_write(path, file, file_size);
    free(file);

    if (err == ESP_OK) {
        char size[24];
        services::storage_human_size(size, sizeof(size), file_size);
        lv_label_set_text_fmt(footer_note_, "saved %s (%s)", services::storage_basename(path), size);
        ESP_LOGI(TAG, "saved %s (%u bytes)", path, (unsigned)file_size);
    } else {
        lv_label_set_text_fmt(footer_note_, "save failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
}

/* ------------------------------------------------------------------------ */
/* events                                                                   */
/* ------------------------------------------------------------------------ */

void DrawingPage::pen_cb(lv_event_t *e)
{
    DrawingPage *self = static_cast<DrawingPage *>(lv_event_get_user_data(e));
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (self == nullptr || btn == nullptr) {
        return;
    }
    const intptr_t index = reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn));
    if (index < 0) {
        return;
    }

    if (index < kSwatchCount) {
        self->pen_color_ = lv_color_hex(kSwatches[index].rgb);
        for (int i = 0; i < kSwatchCount; ++i) {
            if (self->swatches_[i] == nullptr) {
                continue;
            }
            const bool on = (i == (int)index);
            lv_obj_set_style_border_color(self->swatches_[i],
                                          lv_color_hex(on ? Theme::kAccent : Theme::kBorder), 0);
            lv_obj_set_style_border_width(self->swatches_[i], on ? 2 : 1, 0);
        }
        if (self->footer_note_ != nullptr) {
            lv_label_set_text_fmt(self->footer_note_, "pen: %s, %d px",
                                  kSwatches[index].name, (int)self->pen_width_);
        }
        return;
    }

    const int wi = (int)index - kSwatchCount;
    if (wi < 0 || wi >= kWidthCount) {
        return;
    }
    self->pen_width_ = kWidths[wi];
    for (int i = 0; i < kWidthCount; ++i) {
        if (self->widths_[i] == nullptr) {
            continue;
        }
        const bool on = (i == wi);
        lv_obj_set_style_border_color(self->widths_[i],
                                      lv_color_hex(on ? Theme::kAccent : Theme::kBorder), 0);
        lv_obj_set_style_border_width(self->widths_[i], on ? 2 : 1, 0);
    }
    if (self->footer_note_ != nullptr) {
        lv_label_set_text_fmt(self->footer_note_, "pen: %d px", (int)self->pen_width_);
    }
}

void DrawingPage::clear_cb(lv_event_t *e)
{
    DrawingPage *self = static_cast<DrawingPage *>(lv_event_get_user_data(e));
    if (self == nullptr) {
        return;
    }
    self->clear();
    self->have_last_ = false;
    if (self->footer_note_ != nullptr) {
        lv_label_set_text(self->footer_note_, "canvas cleared");
    }
}

void DrawingPage::save_cb(lv_event_t *e)
{
    DrawingPage *self = static_cast<DrawingPage *>(lv_event_get_user_data(e));
    if (self != nullptr) {
        self->save();
    }
}

void DrawingPage::back_cb(lv_event_t *)
{
    app::go_back();
}
