/**
  ******************************************************************************
  * @file    app_page.h
  * @brief   Page descriptor and the menu navigation API.
  *
  *  The reference design (TRE_Flowers) keeps its GUI apps in a linked list and
  *  swaps them in and out of one screen.  The same idea is used here, trimmed
  *  to what a 240x240 panel and a single-threaded main loop actually need:
  *
  *    - every page is a const descriptor with four optional callbacks
  *    - pages register themselves at boot, the menu builds its list from the
  *      registry, so adding a feature is one file plus one register call
  *    - navigation is one level deep (menu -> page -> menu), which is all the
  *      screen has room for
  *
  *  full_screen pages
  *  -----------------
  *  The NES page paints the panel directly through LCD_CopyBuffer() instead of
  *  going through LVGL: pushing a 240x240 frame through the LVGL pipeline at
  *  60 Hz would waste most of the SPI budget on redundant blending.  While such
  *  a page is active the main loop stops calling lv_timer_handler(), and the
  *  screen is invalidated on the way out so LVGL repaints from scratch.
  ******************************************************************************
  */
#ifndef __APP_PAGE_H
#define __APP_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "bsp_key.h"

typedef struct
{
    const char *title;      /**< menu entry, UTF-8                         */
    const char *hint;       /**< one line of help under the title          */
    const char *cmd;        /**< ASCII handle for "open <cmd>" on the wire */
    uint8_t     full_screen;/**< 1: the page owns the panel, LVGL paused   */
    const lv_img_dsc_t *icon;/**< 48x48 menu icon (NULL = no icon)         */

    /** Build the page into root (an empty, full-screen LVGL object). */
    void (*on_enter)(lv_obj_t *root);

    /** Tear down anything the page allocated outside LVGL. */
    void (*on_exit)(void);

    /** Key edge while the page is on top.  Return 1 to swallow the event,
     *  0 to let the menu handle it (BACK closes the page by default). */
    int  (*on_key)(key_id_t id, key_edge_t edge);

    /** Called every main-loop pass while the page is on top. */
    void (*on_tick)(void);

    /** Optional: pages that only *sometimes* own the panel (the emulator owns
     *  it while a game runs but not while its ROM browser is up) answer here.
     *  NULL means "use the static full_screen flag". */
    int  (*wants_display)(void);
} app_page_t;

/*----------------------------------------------------------------------------
 *  Menu
 *--------------------------------------------------------------------------*/

#define APP_PAGE_MAX        8

void              app_menu_init(void);
void              app_menu_register(const app_page_t *page);

int               app_menu_count(void);
const app_page_t *app_menu_page(int index);

/** -1 when the root menu is showing, otherwise the open page index. */
int               app_menu_current(void);
int               app_menu_selection(void);
int               app_menu_is_full_screen(void);

int               app_menu_open_index(int index);
int               app_menu_open_cmd(const char *cmd);
void              app_menu_back(void);
void              app_menu_select(int index);

void              app_menu_handle_key(key_id_t id, key_edge_t edge);
void              app_menu_tick(void);

/** Shared toast line at the bottom of the root menu (NULL clears it). */
void              app_menu_status(const char *text);

/*----------------------------------------------------------------------------
 *  Page factories
 *--------------------------------------------------------------------------*/

const app_page_t *page_clock_get(void);
const app_page_t *page_sysinfo_get(void);
const app_page_t *page_keytest_get(void);
const app_page_t *page_nes_get(void);
const app_page_t *page_image_get(void);
const app_page_t *page_about_get(void);

/*----------------------------------------------------------------------------
 *  NES page hooks used by the console parser
 *--------------------------------------------------------------------------*/

int         page_nes_rom_count(void);
const char *page_nes_rom_name(int index);
const char *page_nes_dir(void);
void        page_nes_rescan(void);
void        page_nes_request_load(int index);
int         page_nes_is_running(void);
uint32_t    page_nes_fps(void);
void        page_nes_stop(void);

/*----------------------------------------------------------------------------
 *  Image viewer hooks used by the console parser
 *--------------------------------------------------------------------------*/

int         page_image_count(void);
const char *page_image_name(int index);
const char *page_image_dir(void);
void        page_image_rescan(void);
void        page_image_request_show(int index);
int         page_image_is_viewing(void);
void        page_image_close(void);
void        page_image_info(char *out, int out_size);

/*----------------------------------------------------------------------------
 *  Small shared UI helpers (implemented in app_menu.c)
 *--------------------------------------------------------------------------*/

#define UI_W                240
#define UI_H                240
#define UI_PAD              8
#define UI_HDR_H            28

#define COL_BG              0xFFFFFF  /* 白底 */
#define COL_HDR             0xFFFFFF  /* 标题栏背景与屏幕一致 */
#define COL_HDR_TXT         0x0319    /* 深蓝标题文字 */
#define COL_SEL             0x8DFF    /* 浅蓝选中高亮 */
#define COL_TEXT            0x0319    /* 主文字：蓝色 */
#define COL_LABEL           0x64BB    /* 描述文字：浅蓝 */
#define COL_VALUE           0x40E070
#define COL_ACCENT          0x0319    /* 强调/选中图标环：蓝色 */
#define COL_DIM             0x64BB    /* 提示/状态栏：浅蓝 */
#define COL_ERR             0xFF4040
#define COL_SEP             0xB6FF    /* 分隔线：淡蓝 */

lv_obj_t *ui_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                   const lv_font_t *font, uint32_t color, const char *text);

lv_obj_t *ui_label_center(lv_obj_t *parent, lv_coord_t y,
                          const lv_font_t *font, uint32_t color,
                          const char *text);

void      ui_label_right(lv_obj_t *label, lv_coord_t y);
void      ui_separator(lv_obj_t *parent, lv_coord_t y);
lv_obj_t *ui_header(lv_obj_t *parent, const char *title);

#ifdef __cplusplus
}
#endif

#endif /* __APP_PAGE_H */
