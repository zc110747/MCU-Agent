/**
  ******************************************************************************
  * @file    app_menu.c
  * @brief   Root menu, page navigation and the shared LVGL helpers.
  *
  *  Layout
  *
  *      0   ┌───────────────────────────────┐
  *          │  STM32H743  主菜单            │  28 px header
  *     28   ├───────────────────────────────┤
  *          │ ▶ 时钟                        │
  *          │   系统信息                    │  36 px per row,
  *          │   NES 模拟器                  │  5 rows visible
  *          │   按键测试                    │
  *          │   关于                        │
  *    212   ├───────────────────────────────┤
  *          │  ↑↓ 选择   A 进入   B 返回    │  status line
  *    240   └───────────────────────────────┘
  *
  *  Only the highlight moves when the selection changes, so LVGL repaints two
  *  rows instead of the whole list and the SPI stays quiet.
  ******************************************************************************
  */
#include "app_page.h"
#include "lv_font_gbk.h"
#include "menu_icons.h"
#include "drv_spi_oled.h"
#include "drv_rtc.h"
#include <string.h>
#include <stdio.h>

#define CARD_W           240         /* one full-screen-width card            */
#define CARD_H           (UI_H - UI_HDR_H)
#define STATUS_Y         (UI_H - 32)

/*----------------------------------------------------------------------------
 *  State
 *--------------------------------------------------------------------------*/

static const app_page_t *s_pages[APP_PAGE_MAX];
static int               s_page_count = 0;
static int               s_selection  = 0;
static int               s_current    = -1;     /* -1 = root menu */

static lv_obj_t         *s_menu_root  = NULL;
static lv_obj_t         *s_page_root  = NULL;
static lv_obj_t         *s_list       = NULL;
static lv_obj_t         *s_status     = NULL;
static lv_obj_t         *s_cards[APP_PAGE_MAX];
static lv_obj_t         *s_titles[APP_PAGE_MAX];
static lv_obj_t         *s_rings[APP_PAGE_MAX];
static lv_obj_t         *s_glows[APP_PAGE_MAX];
static lv_obj_t         *s_dots[APP_PAGE_MAX];
static lv_obj_t         *s_hdr_clock_menu = NULL;   /* "HH:MM" in root header  */
static lv_obj_t         *s_hdr_clock_page = NULL;   /* "HH:MM" in page header  */

/*----------------------------------------------------------------------------
 *  Shared UI helpers
 *--------------------------------------------------------------------------*/

lv_obj_t *ui_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);

    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl, 0, LV_PART_MAIN);
    lv_label_set_text(lbl, (text != NULL) ? text : "");
    lv_obj_set_pos(lbl, x, y);

    return lbl;
}

lv_obj_t *ui_label_center(lv_obj_t *parent, lv_coord_t y,
                          const lv_font_t *font, uint32_t color,
                          const char *text)
{
    lv_obj_t *lbl = ui_label(parent, 0, y, font, color, text);

    lv_obj_set_width(lbl, UI_W);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(lbl, 0, y);

    return lbl;
}

void ui_label_right(lv_obj_t *label, lv_coord_t y)
{
    lv_obj_set_width(label, UI_W - (2 * UI_PAD));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(label, UI_PAD, y);
}

void ui_separator(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *ln = lv_obj_create(parent);

    lv_obj_remove_style_all(ln);
    lv_obj_set_size(ln, UI_W - (2 * UI_PAD), 1);
    lv_obj_set_pos(ln, UI_PAD, y);
    lv_obj_set_style_bg_color(ln, lv_color_hex(COL_SEP), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, LV_PART_MAIN);
}

lv_obj_t *ui_header(lv_obj_t *parent, const char *title)
{
    lv_obj_t *hdr = lv_obj_create(parent);

    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, UI_W, UI_HDR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(COL_HDR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_PART_MAIN);

    (void)ui_label_center(hdr, 6, &lv_font_gbk_16, COL_HDR_TXT, title);

    /* Live wall-clock on the left edge: "HH:MM" (no seconds, per request). */
    {
        rtc_datetime_t dt;
        char           buf[8];

        if (drv_rtc_get(&dt) == RT_OK)
        {
            snprintf(buf, sizeof(buf), "%02u:%02u",
                     (unsigned)dt.hour, (unsigned)dt.minute);
        }
        else
        {
            snprintf(buf, sizeof(buf), "--:--");
        }

        lv_obj_t *clk = ui_label(hdr, UI_PAD, 6, &lv_font_gbk_16,
                                 COL_HDR_TXT, buf);

        if (parent == s_menu_root)
        {
            s_hdr_clock_menu = clk;
        }
        else
        {
            s_hdr_clock_page = clk;
        }
    }

    return hdr;
}

/*----------------------------------------------------------------------------
 *  Root menu construction
 *--------------------------------------------------------------------------*/

/* The root menu is a horizontal strip of full-screen cards.  Only one card is
 * visible at a time; KEY_LEFT / KEY_RIGHT slide the strip.  The selected icon
 * gets a blue ring (plus a soft light-blue glow) and a blue title so it stands
 * out against the white background - no outer rectangular border. */
static void highlight_card(int index, int on)
{
    if ((index < 0) || (index >= s_page_count) ||
        (s_cards[index] == NULL) || (s_titles[index] == NULL))
    {
        return;
    }

    if (s_rings[index] != NULL)
    {
        lv_obj_set_style_border_color(s_rings[index],
                                      lv_color_hex((on != 0) ? COL_ACCENT : COL_DIM),
                                      LV_PART_MAIN);
        lv_obj_set_style_border_width(s_rings[index], (on != 0) ? 3 : 2,
                                      LV_PART_MAIN);
        lv_obj_set_style_border_opa(s_rings[index], LV_OPA_COVER,
                                    LV_PART_MAIN);
    }

    if (s_glows[index] != NULL)
    {
        lv_obj_set_style_border_opa(s_glows[index],
                                    (on != 0) ? LV_OPA_30 : LV_OPA_TRANSP,
                                    LV_PART_MAIN);
    }

    lv_obj_set_style_text_color(s_titles[index],
                                lv_color_hex((on != 0) ? COL_ACCENT : COL_TEXT),
                                LV_PART_MAIN);

    if (s_dots[index] != NULL)
    {
        lv_obj_set_style_bg_color(s_dots[index],
                                  lv_color_hex((on != 0) ? COL_ACCENT : COL_SEP),
                                  LV_PART_MAIN);
    }
}

static void build_menu(void)
{
    int i;
    int dot_y = UI_H - 7;

    s_list = lv_obj_create(s_menu_root);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, UI_W, CARD_H);
    lv_obj_set_pos(s_list, 0, UI_HDR_H);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_list, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_row(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_list, 0, LV_PART_MAIN);

    lv_obj_t *dot_box = lv_obj_create(s_menu_root);
    lv_obj_remove_style_all(dot_box);
    lv_obj_set_size(dot_box, UI_W - (2 * UI_PAD), 8);
    lv_obj_set_pos(dot_box, UI_PAD, dot_y - 4);
    lv_obj_set_flex_flow(dot_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dot_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(dot_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(dot_box, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < s_page_count; i++)
    {
        lv_obj_t *card = lv_obj_create(s_list);

        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_min_width(card, CARD_W, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        /* Soft light-blue glow ring (only visible when selected). */
        lv_obj_t *glow = lv_obj_create(card);
        lv_obj_remove_style_all(glow);
        lv_obj_set_size(glow, 90, 90);
        lv_obj_set_style_radius(glow, 45, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(glow, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(glow, 8, LV_PART_MAIN);
        lv_obj_set_style_border_color(glow, lv_color_hex(COL_SEL), LV_PART_MAIN);
        lv_obj_set_style_border_opa(glow, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_align(glow, LV_ALIGN_CENTER, 0, -34);
        lv_obj_clear_flag(glow, LV_OBJ_FLAG_SCROLLABLE);
        s_glows[i] = glow;

        /* Main icon circle outline. */
        lv_obj_t *ring = lv_obj_create(card);
        lv_obj_remove_style_all(ring);
        lv_obj_set_size(ring, 74, 74);
        lv_obj_set_style_radius(ring, 37, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(ring, lv_color_hex(COL_DIM), LV_PART_MAIN);
        lv_obj_set_style_border_opa(ring, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_align(ring, LV_ALIGN_CENTER, 0, -34);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
        s_rings[i] = ring;

        const lv_img_dsc_t *ic = s_pages[i]->icon;
        if (ic != NULL)
        {
            lv_obj_t *img = lv_img_create(card);
            lv_img_set_src(img, ic);
            lv_obj_align(img, LV_ALIGN_CENTER, 0, -34);
        }

        /* App name only: larger font, just below the icon ring. */
        s_titles[i] = ui_label_center(card, CARD_H / 2 + 22, &lv_font_gbk_24,
                                      COL_TEXT, s_pages[i]->title);

        s_cards[i] = card;

        lv_obj_t *dot = lv_obj_create(dot_box);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 7, 7);
        lv_obj_set_style_radius(dot, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(COL_SEP), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        s_dots[i] = dot;
    }

    s_status = ui_label(s_menu_root, UI_PAD, STATUS_Y, &lv_font_gbk_12,
                        COL_DIM, "← → 选择   A 进入   B 返回");

    highlight_card(s_selection, 1);
}

/*----------------------------------------------------------------------------
 *  Public API
 *--------------------------------------------------------------------------*/

void app_menu_init(void)
{
    lv_obj_t *scr = lv_scr_act();

    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);

    /* Two sibling containers: the menu and whatever page is open.  Swapping
     * hidden flags is cheaper and far less crash-prone than rebuilding the
     * screen every time the user walks in and out of a page. */
    s_menu_root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_menu_root);
    lv_obj_set_size(s_menu_root, UI_W, UI_H);
    lv_obj_set_pos(s_menu_root, 0, 0);
    lv_obj_set_style_bg_color(s_menu_root, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_menu_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_menu_root, LV_OBJ_FLAG_SCROLLABLE);

    s_page_root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_page_root);
    lv_obj_set_size(s_page_root, UI_W, UI_H);
    lv_obj_set_pos(s_page_root, 0, 0);
    lv_obj_set_style_bg_color(s_page_root, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_page_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_page_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page_root, LV_OBJ_FLAG_HIDDEN);

    (void)ui_header(s_menu_root, "APPS");

    build_menu();
    s_current = -1;
}

void app_menu_register(const app_page_t *page)
{
    if ((page != NULL) && (s_page_count < APP_PAGE_MAX))
    {
        s_pages[s_page_count] = page;
        s_page_count++;
    }
}

int app_menu_count(void)
{
    return s_page_count;
}

const app_page_t *app_menu_page(int index)
{
    if ((index < 0) || (index >= s_page_count))
    {
        return NULL;
    }
    return s_pages[index];
}

int app_menu_current(void)
{
    return s_current;
}

int app_menu_selection(void)
{
    return s_selection;
}

int app_menu_is_full_screen(void)
{
    if (s_current < 0)
    {
        return 0;
    }

    if (s_pages[s_current]->wants_display != NULL)
    {
        return s_pages[s_current]->wants_display();
    }

    return (s_pages[s_current]->full_screen != 0U) ? 1 : 0;
}

void app_menu_select(int index)
{
    if ((index < 0) || (index >= s_page_count) || (index == s_selection))
    {
        return;
    }

    highlight_card(s_selection, 0);
    s_selection = index;
    highlight_card(s_selection, 1);

    if (s_cards[index] != NULL)
    {
        lv_obj_scroll_to_view(s_cards[index], LV_ANIM_OFF);
    }
}

int app_menu_open_index(int index)
{
    const app_page_t *page;

    if ((index < 0) || (index >= s_page_count))
    {
        return -1;
    }

    if (s_current >= 0)
    {
        app_menu_back();
    }

    page = s_pages[index];

    lv_obj_clean(s_page_root);
    /* Old page header (and its clock label) is gone; the new page's on_enter()
     * re-creates it via ui_header() if it has a header.  Null now so a page
     * without a header (e.g. full-screen NES) cannot leave a stale pointer. */
    s_hdr_clock_page = NULL;
    lv_obj_clear_flag(s_page_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_menu_root, LV_OBJ_FLAG_HIDDEN);

    s_current   = index;
    s_selection = index;

    if (page->on_enter != NULL)
    {
        page->on_enter(s_page_root);
    }

    /* Paint the page before the caller returns: a full-screen page stops the
     * LVGL task, so without this it would never get its first frame. */
    (void)lv_timer_handler();

    return 0;
}

int app_menu_open_cmd(const char *cmd)
{
    int i;

    if (cmd == NULL)
    {
        return -1;
    }

    for (i = 0; i < s_page_count; i++)
    {
        if ((s_pages[i]->cmd != NULL) && (strcmp(s_pages[i]->cmd, cmd) == 0))
        {
            return app_menu_open_index(i);
        }
    }

    return -1;
}

void app_menu_back(void)
{
    const app_page_t *page;

    if (s_current < 0)
    {
        return;
    }

    page      = s_pages[s_current];
    s_current = -1;

    if (page->on_exit != NULL)
    {
        page->on_exit();
    }

    lv_obj_clean(s_page_root);
    /* The page header (and its "HH:MM" clock label) is destroyed here, so drop
     * our cached pointer.  Otherwise app_menu_tick() would keep calling
     * lv_label_set_text() on a freed LVGL object every time the minute rolls
     * over -> precise BusFault (dangling pointer).  See 2026-08-13 freeze. */
    s_hdr_clock_page = NULL;
    lv_obj_add_flag(s_page_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_menu_root, LV_OBJ_FLAG_HIDDEN);

    highlight_card(s_selection, 1);

    /* A full-screen page wrote straight to the panel behind LVGL's back, so
     * the whole screen has to be declared dirty or half of it stays as game. */
    lv_obj_invalidate(lv_scr_act());
    (void)lv_timer_handler();
}

void app_menu_handle_key(key_id_t id, key_edge_t edge)
{
    if (s_current >= 0)
    {
        const app_page_t *page = s_pages[s_current];

        if (page->on_key != NULL)
        {
            if (page->on_key(id, edge) != 0)
            {
                return;                     /* page consumed the event */
            }
        }

        if ((edge == KEY_EV_DOWN) && ((id == KEY_BACK) || (id == KEY_B)))
        {
            app_menu_back();
        }
        return;
    }

    if (edge != KEY_EV_DOWN)
    {
        return;
    }

    switch (id)
    {
    case KEY_LEFT:
        app_menu_select((s_selection > 0) ? (s_selection - 1)
                                          : (s_page_count - 1));
        break;

    case KEY_RIGHT:
        app_menu_select((s_selection < (s_page_count - 1)) ? (s_selection + 1)
                                                           : 0);
        break;

    case KEY_OK:
    case KEY_A:
    case KEY_START:
        (void)app_menu_open_index(s_selection);
        break;

    default:
        break;
    }
}

void app_menu_tick(void)
{
    /* Refresh the header clock at most once per minute (HH:MM only). */
    static char s_clk_cache[6] = "";
    rtc_datetime_t dt;
    char           buf[8];

    if (drv_rtc_get(&dt) == RT_OK)
    {
        snprintf(buf, sizeof(buf), "%02u:%02u",
                 (unsigned)dt.hour, (unsigned)dt.minute);
    }
    else
    {
        snprintf(buf, sizeof(buf), "--:--");
    }

    if (strcmp(buf, s_clk_cache) != 0)
    {
        (void)strncpy(s_clk_cache, buf, sizeof(s_clk_cache) - 1);
        s_clk_cache[sizeof(s_clk_cache) - 1] = '\0';

        if (s_hdr_clock_menu != NULL)
        {
            lv_label_set_text(s_hdr_clock_menu, buf);
        }
        if (s_hdr_clock_page != NULL)
        {
            lv_label_set_text(s_hdr_clock_page, buf);
        }
    }

    if (s_current >= 0)
    {
        const app_page_t *page = s_pages[s_current];

        if (page->on_tick != NULL)
        {
            page->on_tick();
        }
    }
}

void app_menu_status(const char *text)
{
    if (s_status == NULL)
    {
        return;
    }

    lv_label_set_text(s_status,
                      (text != NULL) ? text : "↑↓ 选择   A 进入   B 返回");
}
