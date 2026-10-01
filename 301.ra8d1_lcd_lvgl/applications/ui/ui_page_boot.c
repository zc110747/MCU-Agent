/**
 ******************************************************************************
 * @file    ui_page_boot.c
 * @brief   Boot splash + animated action bar (Phase 8).
 *
 *  Replaces the static 8-colour-bar pattern that used to be pushed onto the
 *  panel right after bsp_lcd_init(), before LVGL existed.  The panel now goes
 *  straight from black to this screen, so a reset shows a proper boot
 *  animation instead of a frozen test pattern.
 *
 *  Layout (480x360, origin top-left)
 *
 *      0   ┌─────────────────────────────────────────┐
 *          │           RA8D1 VISION BOARD            │  36 px header
 *          │                                          │
 *    110   │              Booting...                 │  title    (28 px)
 *    160   │  ████████████████░░░░░░░░░░░░░░░░░░░░░  │  action bar
 *    196   │           Initializing RTC              │  caption  (16 px)
 *    222   │                    42 %                 │  percent  (16 px)
 *          │                                          │
 *    300   │        RT-Thread Nano / LVGL v9.1       │  footer   (14 px)
 *          └─────────────────────────────────────────┘
 *
 *  The bar is driven by a 40 ms LVGL timer created here, so the animation
 *  keeps running while the rest of the system settles; the boot sequencer in
 *  lv_port.c uses ui_page_boot_done() to know when to hand over to the main
 *  screen, and ui_page_boot_set() lets it name a real step if it wants to.
 ******************************************************************************
 */
#include "ui_page_boot.h"
#include "ui_common.h"

#define BOOT_TITLE_Y    110
#define BOOT_BAR_Y      160
#define BOOT_CAPTION_Y  196
#define BOOT_PCT_Y      222
#define BOOT_FOOT_Y     300

#define BOOT_BAR_W      360
#define BOOT_BAR_H      16

#define BOOT_TICK_MS    40      /* animation period                  */
#define BOOT_PCT_STEP   2       /* percent added per tick (~2 s total) */

/* Steps the bar walks through while it fills; purely cosmetic, the real work
   already happened in main() before LVGL started. */
static const char *const k_boot_steps[] =
{
    "Core clock / sub-clock",
    "Bringing up SDRAM",
    "Starting MIPI DSI panel",
    "Initializing RTC",
    "Starting LVGL",
    "Ready",
};

#define BOOT_STEP_COUNT (sizeof(k_boot_steps) / sizeof(k_boot_steps[0]))

typedef struct
{
    lv_obj_t *bar;
    lv_obj_t *caption;
    lv_obj_t *pct;
    lv_timer_t *timer;
    int32_t value;          /* current percent          */
    int32_t last_step;      /* last caption index shown */
    bool done;
} boot_ui_t;

static boot_ui_t g_boot;

static void boot_apply(int32_t pct)
{
    int32_t step;

    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }

    g_boot.value = pct;

    if (g_boot.bar != NULL)
    {
        lv_bar_set_value(g_boot.bar, pct, LV_ANIM_OFF);
    }
    if (g_boot.pct != NULL)
    {
        lv_label_set_text_fmt(g_boot.pct, "%d %%", (int) pct);
    }

    /* map percent onto the caption list */
    step = (pct * (int32_t) BOOT_STEP_COUNT) / 100;
    if (step >= (int32_t) BOOT_STEP_COUNT)
    {
        step = (int32_t) BOOT_STEP_COUNT - 1;
    }
    if ((step != g_boot.last_step) && (g_boot.caption != NULL))
    {
        g_boot.last_step = step;
        lv_label_set_text(g_boot.caption, k_boot_steps[step]);
    }
}

static void boot_timer_cb(lv_timer_t *timer)
{
    int32_t next = g_boot.value + BOOT_PCT_STEP;

    LV_UNUSED(timer);

    if (next >= 100)
    {
        boot_apply(100);
        g_boot.done = true;
        if (g_boot.timer != NULL)
        {
            lv_timer_delete(g_boot.timer);
            g_boot.timer = NULL;
        }
        return;
    }

    boot_apply(next);
}

lv_obj_t *ui_page_boot_build(void)
{
    lv_obj_t *scr = ui_common_screen_create();
    lv_obj_t *title;

    (void)ui_common_header(scr, "RA8D1 VISION BOARD");

    title = ui_mk_label_center(scr, BOOT_TITLE_Y, UI_FONT(28), COL_DATE, "Booting...");
    LV_UNUSED(title);

    /* action bar: dark track, accent fill, square corners, no border */
    g_boot.bar = lv_bar_create(scr);
    lv_obj_set_size(g_boot.bar, BOOT_BAR_W, BOOT_BAR_H);
    lv_obj_align(g_boot.bar, LV_ALIGN_TOP_MID, 0, BOOT_BAR_Y);
    lv_bar_set_range(g_boot.bar, 0, 100);
    lv_obj_set_style_radius(g_boot.bar, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_boot.bar, lv_color_hex(COL_BAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_boot.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_boot.bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_boot.bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_boot.bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(g_boot.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_value(g_boot.bar, 0, LV_ANIM_OFF);

    g_boot.caption = ui_mk_label_center(scr, BOOT_CAPTION_Y, UI_FONT(16),
                                        COL_LABEL, k_boot_steps[0]);
    g_boot.pct     = ui_mk_label_center(scr, BOOT_PCT_Y, UI_FONT(16),
                                        COL_VALUE, "0 %");

    (void)ui_mk_label_center(scr, BOOT_FOOT_Y, UI_FONT(14), COL_DIM,
                             "RT-Thread Nano / LVGL v9.1");

    /* reset state and (re)arm the animation */
    g_boot.value     = 0;
    g_boot.last_step = 0;
    g_boot.done      = false;

    if (g_boot.timer != NULL)
    {
        lv_timer_delete(g_boot.timer);
    }
    g_boot.timer = lv_timer_create(boot_timer_cb, BOOT_TICK_MS, NULL);

    return scr;
}

void ui_page_boot_set(int32_t pct, const char *caption)
{
    if (caption != NULL && g_boot.caption != NULL)
    {
        lv_label_set_text(g_boot.caption, caption);
        g_boot.last_step = (pct * (int32_t) BOOT_STEP_COUNT) / 100;
    }

    boot_apply(pct);
}

bool ui_page_boot_done(void)
{
    return g_boot.done;
}
