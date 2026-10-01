/**
 ******************************************************************************
 * @file    ui_page_menu.c
 * @brief   Main menu page - see ui_page_menu.h.
 *
 *  Layout (480x360, origin top-left)
 *
 *      0   ┌─────────────────────────────────────────┐
 *          │            RA8D1 MENU                   │  36 px header
 *     36   ├─────────────────────────────────────────┤
 *          │  > LCD test pattern                     │  40 px rows
 *          │    LCD solid fill                       │
 *          │    LVGL demo screen                     │
 *          │    System info                          │
 *    268   ├─────────────────────────────────────────┤
 *          │  ms: menu up / down / enter             │  hint line
 *    360   └─────────────────────────────────────────┘
 *
 *  Every row is a container with one label; the cursor only restyles the two
 *  rows that changed, so a key press repaints ~80 px of height, not the page.
 ******************************************************************************
 */
#include "ui_page_menu.h"
#include "ui_common.h"

/* Page-specific geometry. */
#define ROW_H 44
#define ROW_Y0 (HDR_H + 12)
#define HINT_Y 322

/* Row action ids - everything except ACTION_NONE is served inside the LVGL
   thread by app_ui.c (menu actions must not run on the msh thread). */
#define ACTION_NONE 0U

typedef struct
{
    const char *label;
    uint8_t     action;
} menu_row_t;

static const menu_row_t g_rows[] =
{
    { "LCD test pattern",  1U },
    { "LCD solid fill",    2U },
    { "LVGL demo screen",  3U },
    { "System info",       4U },
};
#define MENU_COUNT ((uint8_t)(sizeof(g_rows) / sizeof(g_rows[0])))

/* 菜单页状态：控件句柄 + 光标 */
typedef struct
{
    lv_obj_t *scr;
    lv_obj_t *row_box[MENU_COUNT];
    lv_obj_t *row_lbl[MENU_COUNT];
    uint8_t   index;
} menu_page_t;

static menu_page_t g_menu = {0};

/* ---- row styling ---------------------------------------------------------- */
static void row_style(uint8_t i, bool selected)
{
    lv_obj_t *box = g_menu.row_box[i];
    lv_obj_t *lbl = g_menu.row_lbl[i];

    if (box == NULL)
    {
        return;
    }

    lv_obj_set_style_bg_color(box, lv_color_hex(selected ? COL_SEL_BG : COL_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, selected ? LV_OPA_COVER : LV_OPA_TRANSP,
                            LV_PART_MAIN);

    if (lbl != NULL)
    {
        lv_obj_set_style_text_color(lbl,
                                    lv_color_hex(selected ? COL_SEL_TXT : COL_LABEL),
                                    LV_PART_MAIN);
        lv_label_set_text_fmt(lbl, "%s%s",
                              selected ? "> " : "  ", g_rows[i].label);
    }
}

/* ---- build ---------------------------------------------------------------- */
lv_obj_t *ui_page_menu_build(void)
{
    g_menu.scr = ui_common_screen_create();

    (void)ui_common_header(g_menu.scr, "RA8D1 MENU");

    for (uint8_t i = 0U; i < MENU_COUNT; i++)
    {
        lv_obj_t *box = lv_obj_create(g_menu.scr);

        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, UI_W - (2 * UI_PAD), ROW_H - 4);
        lv_obj_set_pos(box, UI_PAD, ROW_Y0 + (int32_t) i * ROW_H);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(box, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_left(box, 12, LV_PART_MAIN);

        g_menu.row_box[i] = box;
        g_menu.row_lbl[i] = ui_mk_label(box, 0, 8, UI_FONT(20), COL_LABEL,
                                        g_rows[i].label);
    }

    (void)ui_mk_separator(g_menu.scr, HINT_Y - 8);

    lv_obj_t *hint = ui_mk_label(g_menu.scr, UI_PAD, HINT_Y, UI_FONT(14),
                                 COL_DIM,
                                 "msh: menu up / menu down / menu enter");

    (void)hint;

    g_menu.index = 0U;
    for (uint8_t i = 0U; i < MENU_COUNT; i++)
    {
        row_style(i, i == 0U);
    }

    return g_menu.scr;
}

/* ---- cursor --------------------------------------------------------------- */
void ui_page_menu_move(int delta)
{
    uint8_t old = g_menu.index;
    int     nxt = (int) old + delta;

    if (nxt < 0)
    {
        nxt = 0;
    }
    if (nxt > (int) MENU_COUNT - 1)
    {
        nxt = (int) MENU_COUNT - 1;
    }
    if ((uint8_t) nxt == old)
    {
        return;
    }

    g_menu.index = (uint8_t) nxt;
    row_style(old, false);
    row_style(g_menu.index, true);
}

void ui_page_menu_select(uint8_t index)
{
    uint8_t old = g_menu.index;

    if (index >= MENU_COUNT)
    {
        index = (uint8_t) (MENU_COUNT - 1U);
    }
    if (index == old)
    {
        return;
    }

    g_menu.index = index;
    row_style(old, false);
    row_style(index, true);
}

uint8_t ui_page_menu_enter(void)
{
    return g_rows[g_menu.index].action;
}

uint8_t ui_page_menu_index(void)
{
    return g_menu.index;
}

const char *ui_page_menu_label(uint8_t index)
{
    if (index >= MENU_COUNT)
    {
        return "";
    }
    return g_rows[index].label;
}

uint8_t ui_page_menu_count(void)
{
    return MENU_COUNT;
}

/* ---- optional pointer input (touch panel, if fitted) ----------------------
   The rows are made clickable and each CLICKED event moves the cursor onto
   that row; pressing is followed by the normal LVGL click flow, and the row
   is entered only if the app wires ENTER separately.  Registering the indev
   itself happened in lv_port.c - here it only marks the objects as targets. */
static void menu_click_cb(lv_event_t *e)
{
    lv_obj_t *tgt = lv_event_get_target(e);

    for (uint8_t i = 0U; i < MENU_COUNT; i++)
    {
        if (tgt == g_menu.row_box[i])
        {
            ui_page_menu_select(i);
            return;
        }
    }
}

void ui_page_menu_set_input(lv_indev_t *indev)
{
    if (indev == NULL)
    {
        return;
    }

    /* The indev handle itself is not stored: LVGL routes pointer events to
       whichever object is under the cursor, so only the per-row callbacks
       below are needed. */
    (void) indev;

    for (uint8_t i = 0U; i < MENU_COUNT; i++)
    {
        if (g_menu.row_box[i] != NULL)
        {
            lv_obj_add_flag(g_menu.row_box[i], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(g_menu.row_box[i], menu_click_cb,
                                LV_EVENT_CLICKED, NULL);
        }
    }
}
