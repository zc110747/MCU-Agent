/**
  ******************************************************************************
  * @file    sd_browser.c
  * @brief   Shared SD-card file picker (see sd_browser.h).
  ******************************************************************************
  */
#include "sd_browser.h"
#include "lv_font_gbk.h"
#include "menu_icons.h"
#include "drv_spi_oled.h"
#include "ff.h"
#include "gbk_conv.h"
#include <stdio.h>
#include <string.h>

#define SB_MAX       64
#define SB_NAME      40      /* GBK name as returned by FatFs */
#define SB_DISP      160     /* UTF-8 display name (GBK expanded to UTF-8) */

struct sd_browser
{
    lv_obj_t *root;
    char      path[64];
    char      title[24];
    int       rows_visible;
    int       row_h;

    char      names[SB_MAX][SB_NAME];   /* GBK, verbatim for f_open */
    char      disp[SB_MAX][SB_DISP];    /* UTF-8, for the label */
    int       is_dir[SB_MAX];           /* 1 = directory (or the ".." up entry) */
    int       count;
    int       sel;

    lv_obj_t *list;
    lv_obj_t *status;
    lv_obj_t *rows[SB_MAX];
    lv_obj_t *marks[SB_MAX];
    lv_obj_t *err;

    sd_select_cb on_select;
    void        *ctx;
    int        (*filter)(const char *name, int is_dir);
};

/* Only one picker is ever on screen at a time, so a single static instance is
 * simpler (and safer) than heap allocation inside the LVGL pool. */
static sd_browser_t s_b;
static int          s_used = 0;

/* Truncating a card name that is longer than SB_NAME is intentional (such a
 * file could not be opened through this buffer anyway), but strncpy() makes
 * -Wstringop-truncation fire on it, so the cut is made explicit. */
static void copy_name(char *dst, size_t size, const char *src)
{
    size_t n = strlen(src);

    if (n >= size)
    {
        n = size - 1U;
    }
    (void)memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ".", ".." and volume labels are never shown. */
static int is_dot_or_label(const char *name)
{
    if (name[0] != '.')
    {
        return 0;
    }
    return (name[1] == '\0') || ((name[1] == '.') && (name[2] == '\0'));
}

/* Append one entry.  `dir` marks directories so the key handler can descend
 * instead of calling on_select.  `disp_override` (when non-NULL) replaces the
 * GBK->UTF-8 conversion - used for the synthetic ".." entry. */
static int entry_add(sd_browser_t *b, const char *name_gbk, int dir,
                     const char *disp_override)
{
    if (b->count >= SB_MAX)
    {
        return 0;
    }
    int n = b->count;

    copy_name(b->names[n], sizeof(b->names[n]), name_gbk);
    if (disp_override != NULL)
    {
        copy_name(b->disp[n], sizeof(b->disp[n]), disp_override);
    }
    else
    {
        gbk_to_utf8(b->names[n], b->disp[n], (int)sizeof(b->disp[n]));
    }
    b->is_dir[n] = dir;
    b->count = n + 1;
    return 1;
}

static void scan(sd_browser_t *b)
{
    DIR     dir;
    FILINFO fno;
    int     at_root;

    b->count = 0;
    b->sel   = 0;

    at_root = (strcmp(b->path, "1:") == 0);

    /* ".." to climb one level, shown first when not already at the root. */
    if (!at_root)
    {
        (void)entry_add(b, "..", 1, ".. 上级目录");
    }

    /* Two passes: directories first, then files (both in readdir order). */
    if (f_opendir(&dir, b->path) == FR_OK)
    {
        while (b->count < SB_MAX)
        {
            if (f_readdir(&dir, &fno) != FR_OK)
            {
                break;
            }
            if (fno.fname[0] == '\0')
            {
                break;                              /* end of directory */
            }
            if (is_dot_or_label(fno.fname))
            {
                continue;
            }
            if ((fno.fattrib & AM_DIR) != 0U)
            {
                (void)entry_add(b, fno.fname, 1, NULL);
            }
        }
        (void)f_closedir(&dir);
    }

    if (f_opendir(&dir, b->path) == FR_OK)
    {
        while (b->count < SB_MAX)
        {
            if (f_readdir(&dir, &fno) != FR_OK)
            {
                break;
            }
            if (fno.fname[0] == '\0')
            {
                break;
            }
            if (is_dot_or_label(fno.fname))
            {
                continue;
            }
            if ((fno.fattrib & AM_DIR) == 0U)
            {
                /* Files only: skip when a filter says so (directories always
                 * pass, so the user can still navigate into folders). */
                if ((b->filter != NULL) &&
                    (b->filter(fno.fname, 0) == 0))
                {
                    continue;
                }
                (void)entry_add(b, fno.fname, 0, NULL);
            }
        }
        (void)f_closedir(&dir);
    }
}

static void highlight(sd_browser_t *b, int index, int on)
{
    if ((index < 0) || (index >= b->count) || (b->rows[index] == NULL))
    {
        return;
    }
    lv_obj_set_style_bg_color(b->rows[index],
                              lv_color_hex((on != 0) ? COL_SEL : COL_BG),
                              LV_PART_MAIN);
    lv_label_set_text(b->marks[index], (on != 0) ? ">" : " ");
}

static void select_move(sd_browser_t *b, int dir)
{
    int s;

    if (b->count == 0)
    {
        return;
    }

    s = b->sel;
    s = (s + dir + b->count) % b->count;

    if (s != b->sel)
    {
        highlight(b, b->sel, 0);
        b->sel = s;
        highlight(b, b->sel, 1);
        lv_obj_scroll_to_view(b->rows[s], LV_ANIM_OFF);
    }
}

/* (Re)build the scrollable list + footer underneath the header. */
static void layout_list(sd_browser_t *b)
{
    int i;

    if (b->list != NULL)
    {
        lv_obj_del(b->list);
        b->list = NULL;
    }
    if (b->status != NULL)
    {
        lv_obj_del(b->status);
        b->status = NULL;
    }
    sd_browser_hide_error(b);

    if (b->count == 0)
    {
        (void)ui_label(b->root, UI_PAD, 60, &lv_font_gbk_16, COL_ERR,
                       "未找到文件");
        (void)ui_label(b->root, UI_PAD, 90, &lv_font_gbk_12, COL_DIM,
                       "请检查 SD 卡根目录");
        return;
    }

    b->list = lv_obj_create(b->root);
    lv_obj_remove_style_all(b->list);
    lv_obj_set_size(b->list, UI_W, b->rows_visible * b->row_h);
    lv_obj_set_pos(b->list, 0, UI_HDR_H + 4);
    lv_obj_set_style_bg_opa(b->list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_scroll_dir(b->list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(b->list, LV_SCROLLBAR_MODE_OFF);

    for (i = 0; i < b->count; i++)
    {
        lv_obj_t *row = lv_obj_create(b->list);

        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, UI_W, b->row_h);
        lv_obj_set_pos(row, 0, (lv_coord_t)(i * b->row_h));
        lv_obj_set_style_bg_color(row, lv_color_hex(COL_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        /* Vertically centre each label inside the row (align to LEFT_MID so it
         * does not depend on the font's line_height metric). */
        lv_obj_t *lbl_mark = ui_label(row, 0, 0, &lv_font_gbk_16, COL_ACCENT, " ");
        lv_obj_align(lbl_mark, LV_ALIGN_LEFT_MID, 4, 0);
        b->marks[i] = lbl_mark;

        /* Directories are tagged with a folder icon (white, like files); the
         * icon - not colour - is what marks a directory, so every entry stays
         * equally readable. */
        lv_obj_t *lbl_text;
        if (b->is_dir[i] != 0)
        {
            lv_obj_t *ico = lv_img_create(row);
            lv_img_set_src(ico, &icon_folder);
            lv_obj_align(ico, LV_ALIGN_LEFT_MID, 16, 0);

            lbl_text = ui_label(row, 0, 0, &lv_font_gbk_16, COL_TEXT, b->disp[i]);
            lv_obj_align(lbl_text, LV_ALIGN_LEFT_MID, 34, 0);
        }
        else
        {
            lbl_text = ui_label(row, 0, 0, &lv_font_gbk_16, COL_TEXT, b->disp[i]);
            lv_obj_align(lbl_text, LV_ALIGN_LEFT_MID, 20, 0);
        }

        b->rows[i] = row;
    }

    ui_separator(b->root,
                 UI_HDR_H + 4 + (b->rows_visible * b->row_h) + 4);

    /* The footer hint reflects the navigation level: inside a sub-directory
     * SELECT climbs one level, at the root it exits the application. */
    const char *hint = (strcmp(b->path, "1:") == 0)
                       ? "↑↓ 选  A 进入/打开  SELECT 退出"
                       : "↑↓ 选  A 进入/打开  SELECT 上级";
    b->status = ui_label(b->root, UI_PAD, UI_H - 26, &lv_font_gbk_12,
                         COL_DIM, hint);

    highlight(b, b->sel, 1);
}

sd_browser_t *sd_browser_create(lv_obj_t *root, const char *path,
                                const char *title,
                                int rows_visible, int row_h,
                                sd_select_cb on_select, void *ctx,
                                int (*filter)(const char *name, int is_dir))
{
    if (s_used != 0)
    {
        /* Should never happen (one page at a time), but be safe. */
        sd_browser_destroy(&s_b);
    }

    memset(&s_b, 0, sizeof(s_b));
    s_used = 1;

    s_b.root         = root;
    s_b.rows_visible = rows_visible;
    s_b.row_h        = row_h;
    s_b.on_select    = on_select;
    s_b.ctx          = ctx;
    s_b.filter       = filter;
    (void)strncpy(s_b.path, path, sizeof(s_b.path) - 1U);
    s_b.path[sizeof(s_b.path) - 1U] = '\0';
    (void)strncpy(s_b.title, (title != NULL) ? title : "",
                  sizeof(s_b.title) - 1U);
    s_b.title[sizeof(s_b.title) - 1U] = '\0';

    scan(&s_b);

    (void)ui_header(root, s_b.title);
    layout_list(&s_b);

    return &s_b;
}

void sd_browser_destroy(sd_browser_t *b)
{
    if ((b == NULL) || (s_used == 0))
    {
        return;
    }
    /* The page cleans `root` on exit, so we only reset our own state here. */
    memset(b, 0, sizeof(*b));
    s_used = 0;
}

void sd_browser_refresh(sd_browser_t *b)
{
    if ((b == NULL) || (s_used == 0))
    {
        return;
    }
    scan(b);
    layout_list(b);
}

/* Move the highlight to `index` (clamped to the list) and scroll it into view.
 * Used to resume a re-created browser on the file the user was just reading
 * instead of snapping back to the top. */
void sd_browser_set_sel(sd_browser_t *b, int index)
{
    if ((b == NULL) || (s_used == 0) || (b->count == 0))
    {
        return;
    }
    if (index < 0)
    {
        index = 0;
    }
    if (index >= b->count)
    {
        index = b->count - 1;
    }

    if (index != b->sel)
    {
        highlight(b, b->sel, 0);
        b->sel = index;
        highlight(b, b->sel, 1);
    }
    if (b->rows[b->sel] != NULL)
    {
        lv_obj_scroll_to_view(b->rows[b->sel], LV_ANIM_OFF);
    }
}

/* Enter a subdirectory (path is always rebuilt without a trailing slash). */
static void browser_enter(sd_browser_t *b, const char *sub)
{
    char np[128];

    (void)snprintf(np, sizeof(np), "%s/%s", b->path, sub);
    copy_name(b->path, sizeof(b->path), np);
    scan(b);
    layout_list(b);
}

/* Climb one level.  `b->path` is "1:" at the root (no slash), so strrchr only
 * matches once we are actually inside a subdirectory. */
static void browser_up(sd_browser_t *b)
{
    char *sl = strrchr(b->path, '/');

    if (sl == NULL)
    {
        return;                                 /* already at the root */
    }

    /* Remember the sub-directory we are leaving so we can re-highlight it in
     * the parent list - otherwise the highlight snaps back to the first row and
     * the user loses their place. */
    char sub[SB_NAME];
    (void)strncpy(sub, sl + 1, sizeof(sub) - 1U);
    sub[sizeof(sub) - 1U] = '\0';

    *sl = '\0';
    if (b->path[0] == '\0')
    {
        (void)strcpy(b->path, "1:");
    }
    scan(b);
    layout_list(b);

    for (int i = 0; i < b->count; i++)
    {
        if ((b->is_dir[i] != 0) && (strcmp(b->names[i], sub) == 0))
        {
            sd_browser_set_sel(b, i);
            break;
        }
    }
}

int sd_browser_key(sd_browser_t *b, key_id_t id, key_edge_t edge)
{
    if ((b == NULL) || (s_used == 0))
    {
        return 0;
    }
    if (edge != KEY_EV_DOWN)
    {
        return 0;
    }

    switch (id)
    {
    case KEY_UP:
        select_move(b, -1);
        return 1;

    case KEY_DOWN:
        select_move(b, 1);
        return 1;

    case KEY_A:
    case KEY_OK:
    case KEY_START:
        if (b->count > 0)
        {
            if (b->is_dir[b->sel] != 0)
            {
                if (strcmp(b->names[b->sel], "..") == 0)
                {
                    browser_up(b);
                }
                else
                {
                    browser_enter(b, b->names[b->sel]);
                }
                return 1;
            }
            if (b->on_select != NULL)
            {
                b->on_select(b->sel, b->names[b->sel], b->disp[b->sel], b->ctx);
            }
        }
        return 1;

    case KEY_SELECT:
    case KEY_B:
    case KEY_BACK:
        /* Inside a sub-directory SELECT / B / BACK climb one level (back to the
         * parent, re-highlighting the folder we came from) instead of tearing
         * down the whole application; at the root they fall through to the page,
         * which exits to the main menu. */
        if (strcmp(b->path, "1:") != 0)
        {
            browser_up(b);
        }
        else
        {
            return 0;
        }
        return 1;

    default:
        return 0;
    }
}

int sd_browser_count(sd_browser_t *b)
{
    return ((b != NULL) && (s_used != 0)) ? b->count : 0;
}

int sd_browser_get_sel(sd_browser_t *b)
{
    return ((b != NULL) && (s_used != 0)) ? b->sel : -1;
}

const char *sd_browser_name_gbk(sd_browser_t *b, int i)
{
    if ((b == NULL) || (s_used == 0) || (i < 0) || (i >= b->count))
    {
        return NULL;
    }
    return b->names[i];
}

const char *sd_browser_name_utf8(sd_browser_t *b, int i)
{
    if ((b == NULL) || (s_used == 0) || (i < 0) || (i >= b->count))
    {
        return NULL;
    }
    return b->disp[i];
}

const char *sd_browser_path(sd_browser_t *b)
{
    return ((b != NULL) && (s_used != 0)) ? b->path : "";
}

int sd_browser_is_dir(sd_browser_t *b, int i)
{
    if ((b == NULL) || (s_used == 0) || (i < 0) || (i >= b->count))
    {
        return 0;
    }
    return b->is_dir[i];
}

void sd_browser_show_error(sd_browser_t *b, const char *line1, const char *line2)
{
    if ((b == NULL) || (s_used == 0) || (b->err != NULL))
    {
        return;
    }

    lv_obj_t *ov = lv_obj_create(b->root);

    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, UI_W, UI_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(COL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    (void)ui_label_center(ov, 78, &lv_font_gbk_24, COL_ERR, line1);
    (void)ui_label_center(ov, 116, &lv_font_gbk_16, COL_TEXT,
                          (line2 != NULL) ? line2 : "");
    (void)ui_label_center(ov, UI_H - 30, &lv_font_gbk_12, COL_DIM,
                          "SELECT 返回");

    b->err = ov;
}

void sd_browser_hide_error(sd_browser_t *b)
{
    if ((b != NULL) && (b->err != NULL))
    {
        lv_obj_del(b->err);
        b->err = NULL;
    }
}

int sd_browser_is_error(sd_browser_t *b)
{
    return ((b != NULL) && (b->err != NULL)) ? 1 : 0;
}
