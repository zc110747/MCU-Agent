/**
 * @file widgets.h
 * @brief The shared control set every page builds from.
 *
 * The task spec names seven controls (AppButton, AppCard, AppHeader,
 * AppFooter, IconButton, ListItem, PageContainer).  They live in one
 * translation unit on purpose: they are all thin factories over the same
 * Theme styles, they change together, and splitting them into fourteen files
 * would trade one readable file for a directory listing.
 *
 * Rules every builder here obeys
 * -----------------------------
 *   - no page invents colours, radii or fonts; everything comes from Theme;
 *   - no hard-coded widths: parents distribute space with flex;
 *   - every tappable control is at least Theme::kTouchMin tall;
 *   - `lv_obj_remove_style_all()` first, so LVGL's default theme (which is not
 *     enabled, but is still the documented starting point) can never leak in.
 */
#pragma once

#include "lvgl.h"
#include "theme.h"

namespace ui {

/** @brief The pieces of a standard page skeleton. */
struct PageLayout {
    lv_obj_t *root;          /* column: header / body / footer            */
    lv_obj_t *header;        /* fixed height bar                          */
    lv_obj_t *header_right;  /* empty slot, right aligned                 */
    lv_obj_t *header_close;  /* the exit button, NULL when not requested  */
    lv_obj_t *body;          /* flex-grow, has the safe-area padding      */
    lv_obj_t *footer;        /* fixed height bar, NULL when not requested */
    lv_obj_t *footer_left;   /* empty slot                                */
    lv_obj_t *footer_right;  /* empty slot                                */
};

/**
 * @brief Callback fired by every page's header close button.
 *
 * The button belongs to the ui layer but its *meaning* - "leave this page" -
 * is navigation, which lives in the app layer, and the layering rule here is
 * one-directional (app -> ui).  A direct call would therefore be an upward
 * dependency, so the ui layer owns the button and the app layer injects the
 * action once at start-up (AppManager::init).  Pages get the button for free
 * and cannot forget it; there is no per-page wiring to drift out of sync.
 */
using PageCloseFn = void (*)();

/** @brief Install the action behind the header close button. */
void set_page_close_handler(PageCloseFn fn);

/**
 * @brief Build the standard page skeleton.
 *
 * @param parent       usually the screen
 * @param title        header text
 * @param with_footer  create the bottom bar (most pages want one)
 * @param with_close   add the top-right exit button.  False only for Home:
 *                     Home *is* the application list, so an exit button there
 *                     would be a control that does nothing.
 */
PageLayout page_layout(lv_obj_t *parent, const char *title, bool with_footer = true,
                       bool with_close = true);

/** @brief Text button. @p cb may be NULL. */
lv_obj_t *app_button(lv_obj_t *parent, const char *text,
                     lv_event_cb_t cb = nullptr, void *user = nullptr);

/** @brief Square icon-only button. @p size is the outer box in pixels. */
lv_obj_t *icon_button(lv_obj_t *parent, const lv_image_dsc_t *icon, int size = 44,
                      lv_event_cb_t cb = nullptr, void *user = nullptr);

/** @brief Raised content block; @p title may be NULL. Returns the card. */
lv_obj_t *app_card(lv_obj_t *parent, const char *title = nullptr);

/**
 * @brief One row of a list.
 *
 * @param icon      optional leading glyph (24 px), may be NULL
 * @param title     primary text, required
 * @param subtitle  optional secondary line
 * @param chevron   append a right-pointing chevron (drill-down affordance)
 */
lv_obj_t *list_item(lv_obj_t *parent, const lv_image_dsc_t *icon,
                    const char *title, const char *subtitle = nullptr,
                    bool chevron = true,
                    lv_event_cb_t cb = nullptr, void *user = nullptr);

/**
 * @brief Centred message for empty / error states.
 *
 * Every page that can fail to load data needs one of these, and they all used
 * to look different; this is the single "nothing here" presentation.
 */
lv_obj_t *empty_state(lv_obj_t *parent, const char *icon_text,
                      const char *title, const char *detail);

/**
 * @brief A non-interactive key/value row: title left, value right.
 *
 * Settings, Clock, Weather and the file manager all need to show "label ...
 * value" and none of them should invent their own alignment, so the split is
 * spelled once.  The value is the row's last child, which is what lets
 * info_row_set() update it in place without the page holding a pointer to a
 * label it will forget to clear on destroy.
 */
lv_obj_t *info_row(lv_obj_t *parent, const char *title, const char *value = nullptr);

/** @brief The value label of a row built by info_row(), for %-formatted updates. */
lv_obj_t *info_row_value(lv_obj_t *row);

/** @brief Replace the value text of a row built by info_row(). NULL clears it. */
void info_row_set(lv_obj_t *row, const char *value);

/**
 * @brief A section heading, for use between cards.
 *
 * Rendered as a dimmed 14 px caption rather than a title, because the card
 * below it already carries the title - repeating it at title size made the two
 * read as the same rank.
 */
lv_obj_t *section_label(lv_obj_t *parent, const char *text);

/**
 * @brief A thin horizontal separator that spans its parent.
 *
 * One pixel of Theme::kBorder.  Pages used to draw these with an empty object
 * and a literal height, which drifted between files.
 */
lv_obj_t *divider(lv_obj_t *parent);

}  // namespace ui
