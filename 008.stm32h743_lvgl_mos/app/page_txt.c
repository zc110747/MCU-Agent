/**
  ******************************************************************************
  * @file    page_txt.c
  * @brief   TXT reader page: SD file browser (filtered to *.txt) plus a
  *          paginated, screen-sized text view.
  *
  *  Two states, just like the image viewer:
  *
  *    BROWSER  - the shared sd_browser list, filtered to *.txt so the user only
  *               sees documents they can actually open.  Directories are still
  *               shown, so a .txt buried in a sub-folder is reachable.
  *    READING  - the chosen file is read into RAM, transcoded to UTF-8 and cut
  *               into pages that fit the 240x240 panel.  UP goes to the previous
  *               page, DOWN (or A / OK / START) to the next.  B / SELECT drops
  *               back to the list, SELECT in the list exits to the menu.
  *
  *  Why paging instead of a scroll box
  *  ----------------------------------
  *  The panel is 240x240 and the only input is a serial line, so there is no
  *  thumb to fling.  Paging by whole screenfuls matches the "上下翻页" request
  *  and keeps the rendered frame small and cheap - one LVGL label per page, no
  *  per-character scroll bookkeeping.
  *
  *  Encoding
  *  --------
  *  Card filenames are GBK (FF_CODE_PAGE = 936).  File *contents* follow no
  *  hard rule, so we assume GBK and transcode to UTF-8 with the same
  *  gbk_to_utf8() the rest of the firmware uses; a UTF-8 BOM (EF BB BF) is
  *  detected and honoured (no transcoding) so UTF-8 .txt files are not mangled.
  *
  *  The whole file is read into a RAM buffer (capped) and paginated in place.
  *  The cap is generous for an SD-card reader on an H7 with ~250 KB of AXI-SRAM
  *  still free; files larger than the cap load their first chunk only, and the
  *  footer says so.
  ******************************************************************************
  */
#include "app_page.h"
#include "menu_icons.h"
#include "sd_browser.h"
#include "lv_font_gbk.h"
#include "misc/lv_txt.h"     /* _lv_txt_get_next_line() for word-wrap paging */
#include "drv_spi_oled.h"
#include "bsp_key.h"
#include "ff.h"
#include "gbk_conv.h"        /* GBK (card content) -> UTF-8 for the OLED font driver */
#include <stdio.h>
#include <string.h>

#define TXT_DIR             "1:"

#define TXT_MAX_FILES       64
#define TXT_NAME_LEN        40
#define TXT_ROWS_VISIBLE    6
#define TXT_ROW_H           26

/* Reader geometry ----------------------------------------------------------*/
#define TXT_BODY_FONT       (&lv_font_gbk_16)
#define TXT_BODY_Y          (UI_HDR_H + 18)            /* below the file title  */
#define TXT_USABLE_W        (UI_W - (2 * UI_PAD))      /* 224 px wrap boundary  */
#define TXT_LINES_PER_PAGE  8                          /* screen lines per page  */
#define TXT_FOOTER_Y        (UI_H - 20)

/* Buffers ------------------------------------------------------------------*/
#define TXT_RAW_MAX         32768U                     /* raw bytes read in     */
#define TXT_UTF8_MAX        65536U                     /* transcoded text       */
#define TXT_PAGE_OFF_MAX    1024U                      /* page-start offset cache */
#define TXT_PAGE_TEXT_MAX   1536                       /* one rendered page     */

typedef enum
{
    TXT_STATE_BROWSER = 0,
    TXT_STATE_READING
} txt_state_t;

/*----------------------------------------------------------------------------
 *  State
 *--------------------------------------------------------------------------*/

static txt_state_t    s_state   = TXT_STATE_BROWSER;
static lv_obj_t      *s_root    = NULL;
static sd_browser_t  *s_browser = NULL;

/* Directory the list / file currently lives in ("1:" at the root, "1:/SUB"
 * inside a sub-folder).  Lets back_to_browser() rebuild at the same level
 * instead of always snapping back to the card root. */
static char           s_browse_path[64];

/* Console-side table: *.txt only, so "txt list / open N" has a stable index. */
static char          s_names[TXT_MAX_FILES][TXT_NAME_LEN];
static int           s_count = 0;

/* Reader buffers (static -> .bss in AXI-SRAM). */
static uint8_t       s_raw[TXT_RAW_MAX];
static char          s_utf8[TXT_UTF8_MAX];
static uint32_t      s_utf8_len = 0U;
static uint32_t      s_pg_off[TXT_PAGE_OFF_MAX];
static char          s_page_text[TXT_PAGE_TEXT_MAX];
static int           s_page     = 0;
static int           s_has_next = 0;
static int           s_truncated = 0;
static int           s_is_utf8  = 0;          /* 1 = BOM seen, no transcode */
static char          s_fname_gbk[TXT_NAME_LEN];

/* Live widgets (rebuilt each time the reader view is entered). */
static lv_obj_t     *s_file_lbl = NULL;
static lv_obj_t     *s_body_lbl = NULL;
static lv_obj_t     *s_foot_lbl = NULL;

static int           s_pending_open = -1;

/*----------------------------------------------------------------------------
 *  Discovery (console side)
 *--------------------------------------------------------------------------*/

static int has_txt_suffix(const char *name)
{
    size_t n = strlen(name);

    if (n < 5U)
    {
        return 0;
    }
    return (((name[n - 4] == '.')) &&
            ((name[n - 3] == 't') || (name[n - 3] == 'T')) &&
            ((name[n - 2] == 'x') || (name[n - 2] == 'X')) &&
            ((name[n - 1] == 't') || (name[n - 1] == 'T'))) ? 1 : 0;
}

/* sd_browser filter: only accept .txt files (directories are never filtered). */
static int txt_filter(const char *name, int is_dir)
{
    (void)is_dir;
    return has_txt_suffix(name);
}

/* Forward declaration: on_pick is the sd_browser selection callback used both
 * by open_browser() and back_to_browser() below. */
static void on_pick(int index, const char *name_gbk, const char *name_utf8,
                    void *ctx);

/* Explicit truncation (avoids -Wstringop-truncation in this warning-strict tree). */
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

static void find_txts(void)
{
    DIR     dir;
    FILINFO fno;
    int     n = 0;

    if (f_opendir(&dir, TXT_DIR) != FR_OK)
    {
        s_count = 0;
        return;
    }

    while (n < TXT_MAX_FILES)
    {
        if (f_readdir(&dir, &fno) != FR_OK)
        {
            break;
        }
        if (fno.fname[0] == '\0')
        {
            break;                              /* end of directory */
        }
        if ((fno.fattrib & AM_DIR) != 0U)
        {
            continue;
        }
        if (has_txt_suffix(fno.fname) == 0)
        {
            continue;
        }

        copy_name(s_names[n], sizeof(s_names[n]), fno.fname);
        n++;
    }

    (void)f_closedir(&dir);
    s_count = n;
}

/*----------------------------------------------------------------------------
 *  File load + pagination
 *--------------------------------------------------------------------------*/

/** Read `dir/name` (GBK card names) into the RAM buffers and paginate.
 *  Returns 0 on success, -1 on a hard failure (sets s_fail_reason). */
static int open_file(const char *dir_gbk, const char *name_gbk)
{
    char     path[64];
    FIL      file;
    UINT     read = 0U;
    FSIZE_t  size;
    FRESULT  fr;

    (void)snprintf(path, sizeof(path), "%s/%s", dir_gbk, name_gbk);
    copy_name(s_fname_gbk, sizeof(s_fname_gbk), name_gbk);

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK)
    {
        printf("[TXT ] open %s failed (%d)\r\n", path, (int)fr);
        return -1;
    }

    size = f_size(&file);
    fr   = f_read(&file, s_raw, TXT_RAW_MAX, &read);
    (void)f_close(&file);

    if (fr != FR_OK)
    {
        printf("[TXT ] read %s failed (%d)\r\n", path, (int)fr);
        return -1;
    }

    s_truncated = (size > (FSIZE_t)read) ? 1 : 0;

    /* NUL-terminate the raw chunk so the transcoder can treat it as a string.
     * GBK never contains 0x00 in a valid sequence, so this is safe. */
    s_raw[read] = 0U;

    /* UTF-8 BOM => already UTF-8; otherwise assume GBK and transcode. */
    if ((read >= 3U) && (s_raw[0] == 0xEFU) &&
        (s_raw[1] == 0xBBU) && (s_raw[2] == 0xBFU))
    {
        uint32_t skip = 3U;

        s_is_utf8 = 1;
        (void)memcpy((void *)s_utf8, s_raw + skip, (size_t)(read - skip));
        s_utf8[read - skip] = '\0';
    }
    else
    {
        s_is_utf8 = 0;
        gbk_to_utf8((const char *)s_raw, s_utf8, (int)sizeof(s_utf8));
    }

    s_utf8_len = (uint32_t)strlen(s_utf8);

    s_pg_off[0] = 0U;
    s_page      = 0;
    s_has_next  = 0;

    printf("[TXT ] loaded %s (%lu B, %s%s)\r\n", path,
           (unsigned long)read,
           s_is_utf8 ? "utf8" : "gbk",
           s_truncated ? ", truncated" : "");

    return 0;
}

/** Build the text of page `page` into s_page_text and cache the next page's
 *  start offset in s_pg_off[page + 1]. */
static void build_page(int page)
{
    uint32_t start = s_pg_off[page];
    char    *dst   = s_page_text;
    int      room  = (int)sizeof(s_page_text);
    int      l;

    s_page_text[0] = '\0';

    if (start >= s_utf8_len)
    {
        s_has_next = 0;
        return;                                     /* nothing on this page */
    }

    for (l = 0; l < TXT_LINES_PER_PAGE; l++)
    {
        lv_coord_t used;
        uint32_t   next;

        if (start >= s_utf8_len)
        {
            break;
        }

        /* LVGL's own word-wrap: returns the byte offset of the next line given
         * the panel width, so our page boundaries line up with what the label
         * would draw. */
        next = _lv_txt_get_next_line(s_utf8 + start, TXT_BODY_FONT, 0,
                                    TXT_USABLE_W, &used, LV_TEXT_FLAG_NONE);
        if (next == 0U)
        {
            start = s_utf8_len;                     /* mark EOF so page+1 is empty */
            break;                                  /* end of text */
        }

        /* Drop a single trailing newline (a source line break) so it does not
         * render as a blank line. */
        uint32_t seg = next;
        while ((seg > 0U) && (s_utf8[start + seg - 1U] == '\n'))
        {
            seg--;
        }

        if (room <= 1)
        {
            start += next;
            break;
        }

        int need = (int)seg + 1;                    /* + the '\n' separator */
        if (need > room)
        {
            seg   = (uint32_t)(room - 1);
            need  = room;
        }

        (void)memcpy(dst, s_utf8 + start, seg);
        dst   += seg;
        *dst++ = '\n';
        *dst   = '\0';
        room  -= need;

        start += next;
        if (start >= s_utf8_len)
        {
            break;                                  /* consumed the rest */
        }
    }

    s_pg_off[page + 1] = start;
    s_has_next = (start < s_utf8_len) ? 1 : 0;
}

static void render_page(int page)
{
    if (page < 0)
    {
        page = 0;
    }
    if (page >= (int)TXT_PAGE_OFF_MAX)
    {
        page = (int)TXT_PAGE_OFF_MAX - 1;
    }

    build_page(page);
    s_page = page;

    if (s_body_lbl != NULL)
    {
        lv_label_set_text(s_body_lbl, s_page_text);
    }

    if (s_foot_lbl != NULL)
    {
        if (s_has_next != 0)
        {
            lv_label_set_text_fmt(s_foot_lbl, "第 %d 页  ↑上 ↓/A下  B目录",
                                  page + 1);
        }
        else
        {
            lv_label_set_text_fmt(s_foot_lbl, "第 %d 页(末)  ↑上  B目录",
                                  page + 1);
        }
    }
}

/*----------------------------------------------------------------------------
 *  Reader view
 *--------------------------------------------------------------------------*/

static void build_reader_view(void)
{
    char utf8[160];

    (void)ui_header(s_root, "文本阅读器");

    gbk_to_utf8(s_fname_gbk, utf8, (int)sizeof(utf8));
    s_file_lbl = ui_label(s_root, UI_PAD, UI_HDR_H + 2, &lv_font_gbk_12,
                          COL_DIM, (s_truncated != 0) ? "（仅显示前 32KB）" : utf8);

    s_body_lbl = ui_label(s_root, UI_PAD, TXT_BODY_Y, TXT_BODY_FONT,
                          COL_TEXT, "");
    lv_obj_set_width(s_body_lbl, TXT_USABLE_W);
    lv_label_set_long_mode(s_body_lbl, LV_LABEL_LONG_WRAP);

    s_foot_lbl = ui_label_center(s_root, TXT_FOOTER_Y, &lv_font_gbk_12,
                                 COL_DIM, "↑上 ↓/A下  B目录");

    render_page(0);
}

static void back_to_browser(void)
{
    s_state = TXT_STATE_BROWSER;
    sd_browser_destroy(s_browser);
    s_browser = NULL;
    bsp_key_release_all();

    if (s_root != NULL)
    {
        lv_obj_clean(s_root);
        s_browser = sd_browser_create(s_root, s_browse_path, "文本阅读器",
                                      TXT_ROWS_VISIBLE, TXT_ROW_H,
                                      on_pick, NULL, txt_filter);

        /* Resume the highlight on the file we just read, instead of snapping
         * back to the first row.  Match by GBK name so it works whether the
         * file was opened from the list (on_pick) or via "txt open N"
         * (txt_tick) - open_file() already stored s_fname_gbk in both cases. */
        if (s_fname_gbk[0] != '\0')
        {
            int n = sd_browser_count(s_browser);

            for (int i = 0; i < n; i++)
            {
                if (strcmp(sd_browser_name_gbk(s_browser, i),
                           s_fname_gbk) == 0)
                {
                    sd_browser_set_sel(s_browser, i);
                    break;
                }
            }
        }

        lv_obj_invalidate(lv_scr_act());
        (void)lv_timer_handler();
    }
}

/* Forward decl of on_pick is at the top of the file. */
static void open_browser(lv_obj_t *root)
{
    s_browser = sd_browser_create(root, s_browse_path, "文本阅读器",
                                  TXT_ROWS_VISIBLE, TXT_ROW_H,
                                  on_pick, NULL, txt_filter);
}

/*----------------------------------------------------------------------------
 *  Browser -> reader transition
 *--------------------------------------------------------------------------*/

static void on_pick(int index, const char *name_gbk, const char *name_utf8,
                    void *ctx)
{
    (void)index;
    (void)ctx;

    /* Remember which directory this file lives in, so back_to_browser() can
     * rebuild the list at the same level (not always at the root). */
    (void)strncpy(s_browse_path, sd_browser_path(s_browser),
                  sizeof(s_browse_path) - 1U);
    s_browse_path[sizeof(s_browse_path) - 1U] = '\0';

    if (open_file(sd_browser_path(s_browser), name_gbk) == 0)
    {
        s_state = TXT_STATE_READING;
        sd_browser_destroy(s_browser);
        s_browser = NULL;

        lv_obj_clean(s_root);
        build_reader_view();

        lv_obj_invalidate(lv_scr_act());
        (void)lv_timer_handler();
        return;
    }

    printf("[TXT ] cannot open %s\r\n",
           (name_utf8 != NULL) ? name_utf8 : "?");
    sd_browser_show_error(s_browser, "无法打开", "不是文本文件或读取失败");
}

/*----------------------------------------------------------------------------
 *  Page callbacks
 *--------------------------------------------------------------------------*/

static void txt_enter(lv_obj_t *root)
{
    s_root  = root;
    s_state = TXT_STATE_BROWSER;

    (void)strncpy(s_browse_path, TXT_DIR, sizeof(s_browse_path) - 1U);
    s_browse_path[sizeof(s_browse_path) - 1U] = '\0';

    find_txts();                       /* keeps the console list in sync */
    open_browser(root);
}

static void txt_exit(void)
{
    s_state = TXT_STATE_BROWSER;
    sd_browser_destroy(s_browser);
    s_browser = NULL;
    s_root    = NULL;
    bsp_key_release_all();
}

static int txt_key(key_id_t id, key_edge_t edge)
{
    if (edge != KEY_EV_DOWN)
    {
        return 1;
    }

    if (s_state == TXT_STATE_READING)
    {
        switch (id)
        {
        case KEY_UP:
            if (s_page > 0)
            {
                render_page(s_page - 1);
            }
            return 1;

        case KEY_DOWN:
        case KEY_A:
        case KEY_OK:
        case KEY_START:
            if (s_has_next != 0)
            {
                render_page(s_page + 1);
            }
            return 1;

        case KEY_SELECT:
        case KEY_B:
        case KEY_BACK:
            /* Back to the file list; SELECT/B in the list exits to the menu. */
            back_to_browser();
            return 1;

        default:
            return 1;
        }
    }

    /* The "cannot open" overlay swallows everything until dismissed. */
    if (sd_browser_is_error(s_browser) != 0)
    {
        if ((id == KEY_SELECT) || (id == KEY_B) || (id == KEY_BACK) ||
            (id == KEY_A) || (id == KEY_OK))
        {
            sd_browser_hide_error(s_browser);
        }
        return 1;
    }

    if (sd_browser_key(s_browser, id, edge) != 0)
    {
        return 1;
    }

    if (id == KEY_SELECT)
    {
        app_menu_back();                /* exit to the main menu from the list */
        return 1;
    }

    /* B / BACK fall through so the menu closes the page. */
    return 0;
}

static void txt_tick(void)
{
    /* A console "txt open N" arrived; do the file I/O here so the parser never
     * blocks on the card and never touches LVGL mid-parse. */
    if (s_pending_open >= 0)
    {
        int index = s_pending_open;

        s_pending_open = -1;

        if (s_state == TXT_STATE_READING)
        {
            back_to_browser();
        }

        if ((index >= 0) && (index < s_count))
        {
            /* The console list is always the card root, so resume at root. */
            (void)strncpy(s_browse_path, TXT_DIR, sizeof(s_browse_path) - 1U);
            s_browse_path[sizeof(s_browse_path) - 1U] = '\0';

            if (open_file(TXT_DIR, s_names[index]) == 0)
            {
                s_state = TXT_STATE_READING;
                sd_browser_destroy(s_browser);
                s_browser = NULL;
                lv_obj_clean(s_root);
                build_reader_view();
                lv_obj_invalidate(lv_scr_act());
                (void)lv_timer_handler();
            }
            else
            {
                sd_browser_show_error(s_browser, "无法打开", "读取失败");
            }
        }
    }
}

static const app_page_t s_page_txt =
{
    .title         = "文本阅读器",
    .hint          = "SD 卡看 TXT 文档",
    .cmd           = "txt",
    .full_screen   = 0U,
    .icon          = &icon_txt,
    .on_enter      = txt_enter,
    .on_exit       = txt_exit,
    .on_key        = txt_key,
    .on_tick       = txt_tick
};

const app_page_t *page_txt_get(void)
{
    return &s_page_txt;
}

/*----------------------------------------------------------------------------
 *  Console hooks (used by app_cmd.c)
 *--------------------------------------------------------------------------*/

int page_txt_count(void)
{
    return s_count;
}

const char *page_txt_name(int index)
{
    if ((index < 0) || (index >= s_count))
    {
        return NULL;
    }
    return s_names[index];
}

const char *page_txt_dir(void)
{
    return TXT_DIR;
}

void page_txt_rescan(void)
{
    find_txts();
}

/* Debug helper: write a deterministic multi-page sample to the card so the
 * reader's paging/翻页 can be exercised without a pre-staged document. ASCII
 * only (a subset of GBK), so the GBK->UTF-8 transcode path stays clean. */
int page_txt_seed(const char *name)
{
    char          path[64];
    FIL           file;
    UINT          bw = 0U;
    FRESULT       fr;
    int           i;
    static const char *const tpl[4] =
    {
        "The quick brown fox jumps over the lazy dog.",
        "Pack my box with five dozen liquor jugs, again and again.",
        "How vexingly quick daft zebras jump on a long line that should wrap across more than one screen row on the 240 pixel panel.",
        "Short line."
    };

    if ((name == NULL) || (name[0] == '\0'))
    {
        name = "SEED.TXT";
    }

    /* Optional sub-directory: "SUB/FILE.TXT" creates 1:/SUB first so the
     * reader's sub-folder navigation can be exercised on real hardware. */
    {
        char *slash = strrchr(name, '/');

        if (slash != NULL)
        {
            char dir[48];
            int  dl = (int)(slash - name);

            if (dl >= (int)sizeof(dir))
            {
                dl = (int)sizeof(dir) - 1;
            }
            (void)memcpy(dir, name, (size_t)dl);
            dir[dl] = '\0';
            (void)snprintf(path, sizeof(path), "%s/%s", TXT_DIR, dir);
            fr = f_mkdir(path);
            if ((fr != FR_OK) && (fr != FR_EXIST))
            {
                printf("[TXT ] seed mkdir %s failed (%d)\r\n", path, (int)fr);
                return -1;
            }
        }
    }
    (void)snprintf(path, sizeof(path), "%s/%s", TXT_DIR, name);

    fr = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK)
    {
        printf("[TXT ] seed open %s failed (%d)\r\n", path, (int)fr);
        return -1;
    }

    for (i = 0; i < 80; i++)
    {
        char buf[160];
        int  n = snprintf(buf, sizeof(buf), "Line %03d: %s\n", i, tpl[i & 3]);

        fr = f_write(&file, buf, (UINT)n, &bw);
        if ((fr != FR_OK) || (bw != (UINT)n))
        {
            printf("[TXT ] seed write failed (%d)\r\n", (int)fr);
            (void)f_close(&file);
            return -1;
        }
    }

    (void)f_close(&file);
    printf("[TXT ] seeded %s (80 lines)\r\n", path);
    return 0;
}

void page_txt_request_open(int index)
{
    s_pending_open = index;
}

/* Debug helper: jump the file list into a sub-directory (relative to the card
 * root) so sub-folder navigation can be exercised.  No-op unless the list is
 * currently shown. */
void page_txt_cddir(const char *sub)
{
    char np[64];

    if ((sub == NULL) || (sub[0] == '\0') || (s_state != TXT_STATE_BROWSER))
    {
        return;
    }
    (void)snprintf(np, sizeof(np), "%s/%s", TXT_DIR, sub);
    (void)strncpy(s_browse_path, np, sizeof(s_browse_path) - 1U);
    s_browse_path[sizeof(s_browse_path) - 1U] = '\0';

    sd_browser_destroy(s_browser);
    s_browser = sd_browser_create(s_root, s_browse_path, "文本阅读器",
                                  TXT_ROWS_VISIBLE, TXT_ROW_H,
                                  on_pick, NULL, txt_filter);
    lv_obj_invalidate(lv_scr_act());
    (void)lv_timer_handler();
}

int page_txt_is_reading(void)
{
    return (s_state == TXT_STATE_READING) ? 1 : 0;
}

/* For debug/verification: the browser's current highlight index (the file the
 * user is sitting on in the list), or -1 when not in the list. */
int page_txt_browser_sel(void)
{
    return (s_state == TXT_STATE_BROWSER) ? sd_browser_get_sel(s_browser) : -1;
}

/* For debug/verification: the browser's currently-highlighted display name
 * (UTF-8), or an empty string when not in the list. */
void page_txt_browser_name(char *out, int out_size)
{
    int idx;

    if ((out == NULL) || (out_size <= 0))
    {
        return;
    }
    out[0] = '\0';
    if (s_state != TXT_STATE_BROWSER)
    {
        return;
    }
    idx = sd_browser_get_sel(s_browser);
    if (idx < 0)
    {
        return;
    }
    copy_name(out, (size_t)out_size, sd_browser_name_utf8(s_browser, idx));
}

void page_txt_close(void)
{
    if (s_state == TXT_STATE_READING)
    {
        back_to_browser();
    }
    else
    {
        app_menu_back();
    }
}

/** Fills the caller's buffer with a one-line description of what is shown. */
void page_txt_info(char *out, int out_size)
{
    char utf8[160];

    if ((out == NULL) || (out_size <= 0))
    {
        return;
    }

    if (s_fname_gbk[0] == '\0')
    {
        (void)snprintf(out, (size_t)out_size, "none");
        return;
    }

    gbk_to_utf8(s_fname_gbk, utf8, (int)sizeof(utf8));
    (void)snprintf(out, (size_t)out_size, "%s %s %lu B 第 %d 页%s",
                   utf8,
                   s_is_utf8 ? "utf8" : "gbk",
                   (unsigned long)s_utf8_len,
                   s_page + 1,
                   s_truncated ? " (截断)" : "");
}
