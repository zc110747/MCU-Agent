/**
  ******************************************************************************
  * @file    page_image.c
  * @brief   Image viewer page: SD-root file browser plus a full-screen picture.
  *
  *  Same shape as the NES page, and for the same reason - the two features are
  *  the same interaction with a different payload:
  *
  *    BROWSER  - the shared sd_browser list over the card root.  Everything is
  *               listed, not just *.bmp / *.jpg: hiding a file the user can
  *               see in their card reader is worse than telling them why it
  *               would not open.
  *    VIEWING  - the decoded 240x240 frame owns the panel, LVGL is paused.
  *
  *  Decode first, paint second
  *  --------------------------
  *  img_decode_file() fills its own frame buffer and only then is anything
  *  pushed to the panel, so a truncated JPEG leaves the browser on screen and
  *  raises the error card instead of smearing half a picture over the list.
  ******************************************************************************
  */
#include "app_page.h"
#include "menu_icons.h"
#include "sd_browser.h"
#include "img_decode.h"
#include "lv_font_gbk.h"
#include "drv_spi_oled.h"
#include "bsp_key.h"
#include "ff.h"
#include "gbk_conv.h"
#include <stdio.h>
#include <string.h>

#define IMG_DIR             "1:"

#define IMG_MAX_FILES       32
#define IMG_NAME_LEN        40
#define IMG_ROWS_VISIBLE    6
#define IMG_ROW_H           26

typedef enum
{
    IMG_STATE_BROWSER = 0,
    IMG_STATE_VIEWING
} img_state_t;

/*----------------------------------------------------------------------------
 *  State
 *--------------------------------------------------------------------------*/

static img_state_t   s_state   = IMG_STATE_BROWSER;
static lv_obj_t     *s_root    = NULL;
static sd_browser_t *s_browser = NULL;

/* Console-side table: image extensions only, so `img show N` has a stable
 * index.  The on-screen list is owned by sd_browser and shows every file. */
static char          s_names[IMG_MAX_FILES][IMG_NAME_LEN];
static int           s_count = 0;

static img_info_t    s_info;
static char          s_shown[IMG_NAME_LEN] = "";
static const char   *s_fail_reason         = "";

static int           s_pending_show = -1;

/*----------------------------------------------------------------------------
 *  Discovery (console side)
 *--------------------------------------------------------------------------*/

static int ext_is(const char *name, const char *ext)
{
    size_t n = strlen(name);
    size_t e = strlen(ext);
    size_t i;

    if (n <= e)
    {
        return 0;
    }
    if (name[n - e - 1U] != '.')
    {
        return 0;
    }

    for (i = 0U; i < e; i++)
    {
        char a = name[n - e + i];
        char b = ext[i];

        if ((a >= 'A') && (a <= 'Z'))
        {
            a = (char)(a + ('a' - 'A'));
        }
        if (a != b)
        {
            return 0;
        }
    }
    return 1;
}

/* Explicit truncation: strncpy() into a shorter buffer is what is wanted here
 * but trips -Wstringop-truncation, and this tree builds warning-free. */
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

static int looks_like_image(const char *name)
{
    return ((ext_is(name, "bmp") != 0) ||
            (ext_is(name, "jpg") != 0) ||
            (ext_is(name, "jpeg") != 0)) ? 1 : 0;
}

static void find_images(void)
{
    DIR     dir;
    FILINFO fno;
    int     n = 0;

    if (f_opendir(&dir, IMG_DIR) != FR_OK)
    {
        s_count = 0;
        return;
    }

    while (n < IMG_MAX_FILES)
    {
        if (f_readdir(&dir, &fno) != FR_OK)
        {
            break;
        }
        if (fno.fname[0] == '\0')
        {
            break;
        }
        if ((fno.fattrib & AM_DIR) != 0U)
        {
            continue;
        }
        if (looks_like_image(fno.fname) == 0)
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
 *  Open / close a picture
 *--------------------------------------------------------------------------*/

static void open_browser(lv_obj_t *root);

/** Decode `name` (verbatim GBK card name) and take over the panel. */
static int show_image(const char *name)
{
    char        path[64];
    const char *why = "未知错误";

    if ((name == NULL) || (name[0] == '\0'))
    {
        s_fail_reason = "没有选中文件";
        return -1;
    }

    (void)snprintf(path, sizeof(path), "%s/%s", sd_browser_path(s_browser), name);

    if (img_decode_file(path, &s_info, &why) != 0)
    {
        s_fail_reason = why;
        return -1;
    }

    copy_name(s_shown, sizeof(s_shown), name);

    /* From here on the page owns the display: wants_display() flips, the main
     * loop stops calling lv_timer_handler(), and the frame goes out over SPI6
     * the same way the emulator's frames do. */
    s_state = IMG_STATE_VIEWING;
    img_blit();

    printf("[IMG ] viewing - 按 SELECT / BACK 返回\r\n");
    return 0;
}

/** Drop back to the list and hand the panel back to LVGL. */
static void close_image(void)
{
    s_state = IMG_STATE_BROWSER;
    bsp_key_release_all();

    if (s_root != NULL)
    {
        sd_browser_destroy(s_browser);
        s_browser = NULL;

        lv_obj_clean(s_root);
        open_browser(s_root);

        lv_obj_invalidate(lv_scr_act());
        (void)lv_timer_handler();
    }
}

/*----------------------------------------------------------------------------
 *  Browser
 *--------------------------------------------------------------------------*/

static void on_pick(int index, const char *name_gbk, const char *name_utf8,
                    void *ctx)
{
    (void)index;
    (void)ctx;

    if (show_image(name_gbk) == 0)
    {
        return;
    }

    printf("[IMG ] cannot open %s: %s\r\n",
           (name_utf8 != NULL) ? name_utf8 : "?", s_fail_reason);

    sd_browser_show_error(s_browser, "无法打开", s_fail_reason);
}

static void open_browser(lv_obj_t *root)
{
    s_browser = sd_browser_create(root, IMG_DIR, "图片查看器",
                                  IMG_ROWS_VISIBLE, IMG_ROW_H,
                                  on_pick, NULL, NULL);
}

/*----------------------------------------------------------------------------
 *  Page callbacks
 *--------------------------------------------------------------------------*/

static void image_enter(lv_obj_t *root)
{
    s_root  = root;
    s_state = IMG_STATE_BROWSER;

    find_images();                  /* keeps the console list in sync */
    open_browser(root);
}

static void image_exit(void)
{
    s_state = IMG_STATE_BROWSER;
    sd_browser_destroy(s_browser);
    s_browser = NULL;
    s_root    = NULL;
    bsp_key_release_all();
}

static int image_key(key_id_t id, key_edge_t edge)
{
    if (edge != KEY_EV_DOWN)
    {
        return 1;
    }

    if (s_state == IMG_STATE_VIEWING)
    {
        /* Any of the "get me out" keys drops back to the list; everything else
         * is swallowed so a stray press cannot bubble into the menu. */
        if ((id == KEY_SELECT) || (id == KEY_BACK) || (id == KEY_MENU) ||
            (id == KEY_B) || (id == KEY_A) || (id == KEY_OK))
        {
            close_image();
        }
        return 1;
    }

    /* The "cannot open" overlay swallows everything until it is dismissed. */
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
        app_menu_back();
        return 1;
    }

    /* B / BACK fall through so the menu closes the page. */
    return 0;
}

static void image_tick(void)
{
    /* A console "img show N" arrived; the decode happens here so the command
     * parser never blocks on the card and never touches LVGL mid-parse. */
    if (s_pending_show >= 0)
    {
        int index = s_pending_show;

        s_pending_show = -1;

        if (s_state == IMG_STATE_VIEWING)
        {
            close_image();
        }

        if ((index >= 0) && (index < s_count))
        {
            if (show_image(s_names[index]) != 0)
            {
                sd_browser_show_error(s_browser, "无法打开", s_fail_reason);
            }
        }
    }
}

static int image_wants_display(void)
{
    return (s_state == IMG_STATE_VIEWING) ? 1 : 0;
}

static const app_page_t s_page_image =
{
    .title         = "图片查看器",
    .hint          = "SD 根目录看 BMP/JPG",
    .cmd           = "image",
    .full_screen   = 0U,
    .icon          = &icon_image,
    .on_enter      = image_enter,
    .on_exit       = image_exit,
    .on_key        = image_key,
    .on_tick       = image_tick,
    .wants_display = image_wants_display
};

const app_page_t *page_image_get(void)
{
    return &s_page_image;
}

/*----------------------------------------------------------------------------
 *  Console hooks (used by app_cmd.c)
 *--------------------------------------------------------------------------*/

int page_image_count(void)
{
    return s_count;
}

const char *page_image_name(int index)
{
    if ((index < 0) || (index >= s_count))
    {
        return NULL;
    }
    return s_names[index];
}

const char *page_image_dir(void)
{
    return IMG_DIR;
}

void page_image_rescan(void)
{
    find_images();
}

void page_image_request_show(int index)
{
    s_pending_show = index;
}

int page_image_is_viewing(void)
{
    return (s_state == IMG_STATE_VIEWING) ? 1 : 0;
}

void page_image_close(void)
{
    if (s_state == IMG_STATE_VIEWING)
    {
        close_image();
    }
}

/** Fills the caller's buffer with a one-line description of what is shown. */
void page_image_info(char *out, int out_size)
{
    char utf8[160];

    if ((out == NULL) || (out_size <= 0))
    {
        return;
    }

    if (s_shown[0] == '\0')
    {
        (void)snprintf(out, (size_t)out_size, "none");
        return;
    }

    gbk_to_utf8(s_shown, utf8, (int)sizeof(utf8));
    (void)snprintf(out, (size_t)out_size, "%s %s %dx%d -> %dx%d %lu B",
                   utf8,
                   (s_info.format != NULL) ? s_info.format : "?",
                   s_info.src_w, s_info.src_h,
                   s_info.out_w, s_info.out_h,
                   (unsigned long)s_info.bytes);
}
