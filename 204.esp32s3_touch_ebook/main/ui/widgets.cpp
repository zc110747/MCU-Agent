#include "widgets.h"

#include "app_icons.h"

namespace ui {

/* ------------------------------------------------------------------------ */
/* helpers                                                                  */
/* ------------------------------------------------------------------------ */

static lv_obj_t *make_bar(lv_obj_t *parent, lv_style_t *style, int height)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_add_style(bar, style, 0);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, height);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

static lv_obj_t *make_slot(lv_obj_t *parent, lv_flex_align_t main_align = LV_FLEX_ALIGN_END,
                           bool grow = false)
{
    lv_obj_t *slot = lv_obj_create(parent);
    lv_obj_remove_style_all(slot);
    if (grow) {
        /* A definite (grown) width instead of LV_SIZE_CONTENT: with a content-
         * sized slot the flex main-axis justify can mis-measure and shove the
         * children off-screen (the Calendar "< Prev" ended up at x=-149).  A
         * grown slot has a real width, so START-justified children stay put. */
        lv_obj_set_flex_grow(slot, 1);
    } else {
        lv_obj_set_size(slot, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    }
    lv_obj_set_flex_flow(slot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slot, main_align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(slot, Theme::kGapSm, 0);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    return slot;
}

static lv_obj_t *make_icon(lv_obj_t *parent, const lv_image_dsc_t *icon, uint32_t tint)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, icon);
    /* Every generated glyph is white-with-alpha, so the colour is applied at
     * draw time instead of being baked into the asset.  That is what lets the
     * same file serve a bright header and a dimmed list row.               */
    lv_obj_set_style_image_recolor(img, lv_color_hex(tint), 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    return img;
}

/* ------------------------------------------------------------------------ */
/* page skeleton                                                            */
/* ------------------------------------------------------------------------ */

namespace {
PageCloseFn s_close_fn = nullptr;

void close_clicked_cb(lv_event_t *)
{
    if (s_close_fn != nullptr) {
        s_close_fn();
    }
}
}  // namespace

void set_page_close_handler(PageCloseFn fn)
{
    s_close_fn = fn;
}

PageLayout page_layout(lv_obj_t *parent, const char *title, bool with_footer,
                       bool with_close)
{
    PageLayout p = {};

    p.root = lv_obj_create(parent);
    lv_obj_remove_style_all(p.root);
    lv_obj_add_style(p.root, Theme::screen(), 0);
    lv_obj_set_size(p.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(p.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p.root, 0, 0);
    lv_obj_set_style_pad_row(p.root, 0, 0);
    lv_obj_clear_flag(p.root, LV_OBJ_FLAG_SCROLLABLE);

    p.header = make_bar(p.root, Theme::header(), Theme::kHeaderH);
    lv_obj_t *label = lv_label_create(p.header);
    lv_obj_add_style(label, Theme::text_title(), 0);
    lv_label_set_text(label, title ? title : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(label, 1);

    p.header_right = make_slot(p.header);

    if (with_close) {
        /* Placed last in the header row AND with the slot's right padding
         * widened by its own width, so a page that appends its own chips to
         * header_right (Clock's "24H", Weather's "Refresh") can never end up
         * to the right of the exit button.  A close button that moves around
         * between pages is a bug the user has to hunt for. */
        const int size = Theme::kHeaderH - 2 * Theme::kGapSm;
        p.header_close = icon_button(p.header, &icon_close, size, close_clicked_cb, nullptr);
        lv_obj_set_style_pad_right(p.header_right,
                                   Theme::kSafePad + size + Theme::kGapSm, 0);
    }

    p.body = lv_obj_create(p.root);
    lv_obj_remove_style_all(p.body);
    lv_obj_set_width(p.body, LV_PCT(100));
    lv_obj_set_flex_grow(p.body, 1);
    lv_obj_set_flex_flow(p.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(p.body, Theme::kSafePad, 0);
    lv_obj_set_style_pad_row(p.body, Theme::kGapMd, 0);
    /* Scrollable by default: a page whose content overflows must still be
     * reachable, and a page that does not overflow never shows the bar.    */
    lv_obj_set_scroll_dir(p.body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p.body, LV_SCROLLBAR_MODE_AUTO);

    if (with_footer) {
        p.footer = make_bar(p.root, Theme::footer(), Theme::kFooterH);
        /* footer_left grows to fill the left of the bar and left-aligns its
         * children, so e.g. Calendar's "< Prev" sits at the screen's left edge
         * instead of being pushed off-screen by a mis-sized content slot. */
        p.footer_left = make_slot(p.footer, LV_FLEX_ALIGN_START, true);
        p.footer_right = make_slot(p.footer);
    }
    return p;
}

/* ------------------------------------------------------------------------ */
/* controls                                                                 */
/* ------------------------------------------------------------------------ */

lv_obj_t *app_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, Theme::btn(), 0);
    lv_obj_add_style(btn, Theme::btn_pressed(), LV_STATE_PRESSED);
    lv_obj_set_height(btn, Theme::kTouchMin);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, Theme::font_body(), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(Theme::kText), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    }
    return btn;
}

lv_obj_t *icon_button(lv_obj_t *parent, const lv_image_dsc_t *icon, int size,
                      lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_add_style(btn, Theme::icon_btn(), 0);
    lv_obj_add_style(btn, Theme::icon_btn_pressed(), LV_STATE_PRESSED);
    lv_obj_set_size(btn, size, size);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    if (icon) {
        lv_obj_t *img = make_icon(btn, icon, Theme::kText);
        lv_obj_center(img);
    }

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    }
    return btn;
}

lv_obj_t *app_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, Theme::card(), 0);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, Theme::kGapMd, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    if (title) {
        lv_obj_t *label = lv_label_create(card);
        lv_obj_add_style(label, Theme::text_title(), 0);
        lv_label_set_text(label, title);
    }
    return card;
}

lv_obj_t *list_item(lv_obj_t *parent, const lv_image_dsc_t *icon,
                    const char *title, const char *subtitle, bool chevron,
                    lv_event_cb_t cb, void *user)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, Theme::list_row(), 0);
    lv_obj_add_style(row, Theme::list_row_pressed(), LV_STATE_PRESSED);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, subtitle ? Theme::kRowH + 18 : Theme::kRowH);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, Theme::kGapMd, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    if (icon) {
        lv_obj_t *img = make_icon(row, icon, Theme::kTextDim);
        lv_obj_set_style_image_recolor(img, lv_color_hex(Theme::kAccent), 0);
    }

    lv_obj_t *texts = lv_obj_create(row);
    lv_obj_remove_style_all(texts);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(texts, 1, 0);
    lv_obj_clear_flag(texts, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *primary = lv_label_create(texts);
    lv_obj_add_style(primary, Theme::text_body(), 0);
    lv_label_set_text(primary, title);
    lv_label_set_long_mode(primary, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(primary, LV_PCT(100));

    if (subtitle) {
        lv_obj_t *secondary = lv_label_create(texts);
        lv_obj_add_style(secondary, Theme::text_dim(), 0);
        lv_obj_set_style_text_font(secondary, Theme::font_small(), 0);
        lv_label_set_text(secondary, subtitle);
        lv_label_set_long_mode(secondary, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(secondary, LV_PCT(100));
    }

    if (chevron) {
        lv_obj_t *arrow = make_icon(row, &icon_forward, Theme::kTextDim);
        LV_UNUSED(arrow);
    }

    if (cb) {
        lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);
    }
    return row;
}

lv_obj_t *empty_state(lv_obj_t *parent, const char *icon_text,
                      const char *title, const char *detail)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, Theme::kGapSm, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    if (icon_text) {
        /* A plain text glyph keeps this widget free of any asset dependency,
         * so an empty state can never itself fail to load.                 */
        lv_obj_t *glyph = lv_label_create(box);
        lv_obj_set_style_text_font(glyph, Theme::font_h1(), 0);
        lv_obj_set_style_text_color(glyph, lv_color_hex(Theme::kTextDim), 0);
        lv_label_set_text(glyph, icon_text);
    }

    lv_obj_t *head = lv_label_create(box);
    lv_obj_add_style(head, Theme::text_title(), 0);
    lv_label_set_text(head, title);
    lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_CENTER, 0);

    if (detail) {
        lv_obj_t *sub = lv_label_create(box);
        lv_obj_add_style(sub, Theme::text_dim(), 0);
        lv_label_set_text(sub, detail);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(sub, LV_PCT(80));
    }
    return box;
}

/* ------------------------------------------------------------------------ */
/* key / value rows and small layout helpers                                */
/* ------------------------------------------------------------------------ */

lv_obj_t *info_row(lv_obj_t *parent, const char *title, const char *value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, Theme::kGapMd, 0);
    /* Not scrollable and not clickable: this row is there to be read.  A row
     * that steals clicks would also swallow the card's own scrolling. */
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *key = lv_label_create(row);
    lv_obj_add_style(key, Theme::text_body(), 0);
    lv_label_set_text(key, title);
    lv_label_set_long_mode(key, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(key, 1);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_add_style(val, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(val, Theme::font_small(), 0);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(val, value ? value : "--");
    lv_label_set_long_mode(val, LV_LABEL_LONG_MODE_DOTS);
    /* The value gets the larger share: an SSID or a path is the interesting
     * half of the row and truncating it while the fixed label keeps its width
     * is the wrong way round. */
    lv_obj_set_flex_grow(val, 2);

    return row;
}

lv_obj_t *info_row_value(lv_obj_t *row)
{
    if (row == nullptr) {
        return nullptr;
    }
    /* Built by info_row(), which appends the value last. */
    return lv_obj_get_child(row, -1);
}

void info_row_set(lv_obj_t *row, const char *value)
{
    lv_obj_t *val = info_row_value(row);
    if (val != nullptr) {
        lv_label_set_text(val, (value != nullptr) ? value : "--");
    }
}

lv_obj_t *section_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_add_style(label, Theme::text_dim(), 0);
    lv_obj_set_style_text_font(label, Theme::font_small(), 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *divider(lv_obj_t *parent)
{
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_set_width(line, LV_PCT(100));
    lv_obj_set_height(line, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(Theme::kBorder), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    return line;
}

}  // namespace ui
