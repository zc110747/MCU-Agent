/**
  ******************************************************************************
  * @file    page_nes.c
  * @brief   NES application page: SD-root file browser plus the full-screen
  *          emulator.
  *
  *  Two states share one page descriptor:
  *
  *    BROWSER  - the shared sd_browser list showing *every* file in the card
  *               root.  Picking a file that is not a usable iNES image is a
  *               normal outcome, not a bug: the page raises the "cannot load"
  *               overlay and SELECT dismisses it.  Normal LVGL page, the menu
  *               keeps driving lv_timer_handler().
  *    RUNNING  - the emulator owns the panel.  wants_display() returns 1, the
  *               main loop stops servicing LVGL, and each frame is converted
  *               from palette indices to RGB565 and pushed over SPI6 directly.
  *
  *  Why the browser lists everything
  *  --------------------------------
  *  Filtering to *.nes hides the failure mode instead of reporting it - a
  *  renamed or truncated ROM would silently vanish from the list.  The list is
  *  the card root, the load is attempted on whatever is chosen, and the reason
  *  it failed is put on screen.  The console `rom list` stays filtered to
  *  *.nes because that command is explicitly about ROMs (and `rom load N`
  *  indexes into that filtered list).
  *
  *  Cropping
  *  --------
  *  The NES renders 256x240, the panel is 240x240.  Eight columns are dropped
  *  from each side, which is exactly the overscan region every real TV hid
  *  anyway - no scaling, no filtering, no wasted cycles.
  *
  *  Blitting
  *  --------
  *  A whole RGB565 frame would be 115 kB, so the conversion runs in bands of
  *  NES_BAND_LINES rows through one static buffer.  The buffer lives in .bss
  *  (AXI-SRAM) rather than the emulator's DTCM block because LCD_CopyBuffer()
  *  streams from it.
  ******************************************************************************
  */
#include "app_page.h"
#include "menu_icons.h"
#include "sd_browser.h"
#include "lv_font_gbk.h"
#include "drv_spi_oled.h"
#include "bsp_key.h"
#include "nes.h"
#include "ff.h"
#include "gbk_conv.h"   /* GBK (card name) -> UTF-8 for the OLED font driver */
#include <stdio.h>
#include <string.h>
#include "bsp_log.h"

/* The browser and the console both work off the card root. */
#define NES_DIR             "1:"

#define NES_MAX_ROMS        32
#define NES_NAME_LEN        40
#define NES_ROWS_VISIBLE    6
#define NES_ROW_H           26

/* Rows converted per SPI transaction: 240 x 30 x 2 B = 14.4 kB. */
#define NES_BAND_LINES      30

#define NES_CROP_X          ((NES_SCREEN_W - UI_W) / 2)     /* 8 */

typedef enum
{
    NES_STATE_BROWSER = 0,
    NES_STATE_RUNNING
} nes_state_t;

/*----------------------------------------------------------------------------
 *  State
 *--------------------------------------------------------------------------*/

static nes_state_t s_state = NES_STATE_BROWSER;

/* Console-side ROM table: *.nes only, so `rom load N` keeps meaning what it
 * always did.  The on-screen list is owned by sd_browser and shows everything. */
static char        s_names[NES_MAX_ROMS][NES_NAME_LEN];
static int         s_rom_count = 0;
static char        s_dir[16]   = NES_DIR;

static lv_obj_t     *s_root    = NULL;
static sd_browser_t *s_browser = NULL;

static uint16_t    s_band[UI_W * NES_BAND_LINES];
static uint32_t    s_last_frame_tick = 0U;
static uint32_t    s_fps_window      = 0U;
static uint32_t    s_fps_frames      = 0U;
static uint32_t    s_fps             = 0U;

/* Set from the console so "rom load 2" works while the browser is open. */
static int         s_pending_load = -1;

/* Why the last load attempt failed, in UTF-8, for the error overlay. */
static const char *s_fail_reason = "";

/*----------------------------------------------------------------------------
 *  ROM discovery (console side)
 *--------------------------------------------------------------------------*/

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

static int has_nes_suffix(const char *name)
{
    size_t n = strlen(name);

    if (n < 5U)
    {
        return 0;
    }

    return (((name[n - 4] == '.')) &&
            ((name[n - 3] == 'n') || (name[n - 3] == 'N')) &&
            ((name[n - 2] == 'e') || (name[n - 2] == 'E')) &&
            ((name[n - 1] == 's') || (name[n - 1] == 'S'))) ? 1 : 0;
}

static void find_roms(void)
{
    DIR     dir;
    FILINFO fno;
    int     count = 0;

    copy_name(s_dir, sizeof(s_dir), NES_DIR);

    if (f_opendir(&dir, s_dir) != FR_OK)
    {
        s_rom_count = 0;
        return;
    }

    while (count < NES_MAX_ROMS)
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
        if (has_nes_suffix(fno.fname) == 0)
        {
            continue;
        }

        copy_name(s_names[count], sizeof(s_names[count]), fno.fname);
        count++;
    }

    (void)f_closedir(&dir);
    s_rom_count = count;
}

/*----------------------------------------------------------------------------
 *  Loading
 *--------------------------------------------------------------------------*/

/** Try to load `name` (verbatim GBK card name) out of the root directory.
 *  Returns 0 on success; on failure s_fail_reason describes why in UTF-8. */
static int load_rom_named(const char *name)
{
    char      path[64];
    char      utf8[160];
    FIL       file;
    UINT      read = 0U;
    FSIZE_t   size;
    FRESULT   fr;
    nes_err_t err;

    s_fail_reason = "未知错误";

    if ((name == NULL) || (name[0] == '\0'))
    {
        s_fail_reason = "没有选中文件";
        return -1;
    }

    if (nes_rom_arena() == NULL)
    {
        PRINT_LOG("[NES ] no ROM arena (allocation failed at page open)\r\n");
        s_fail_reason = "内存不足";
        return -1;
    }

    (void)snprintf(path, sizeof(path), "%s/%s", sd_browser_path(s_browser), name);
    gbk_to_utf8(name, utf8, (int)sizeof(utf8));

    fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK)
    {
        PRINT_LOG("[NES ] open %s failed (%d)\r\n", utf8, (int)fr);
        s_fail_reason = "文件打不开";
        return -1;
    }

    size = f_size(&file);

    if (size < 16U)
    {
        (void)f_close(&file);
        PRINT_LOG("[NES ] %s is only %lu B\r\n", utf8, (unsigned long)size);
        s_fail_reason = "不是 NES 文件";
        return -1;
    }

    if (size > nes_rom_arena_size())
    {
        PRINT_LOG("[NES ] %s is %lu B, arena holds %lu B\r\n", utf8,
               (unsigned long)size,
               (unsigned long)nes_rom_arena_size());
        (void)f_close(&file);
        s_fail_reason = "文件过大";
        return -1;
    }

    fr = f_read(&file, nes_rom_arena(), (UINT)size, &read);
    (void)f_close(&file);

    if ((fr != FR_OK) || (read == 0U))
    {
        PRINT_LOG("[NES ] read %s failed (%d)\r\n", utf8, (int)fr);
        s_fail_reason = "读取失败";
        return -1;
    }

    err = nes_load((uint32_t)read);
    if (err != NES_OK)
    {
        static const char *reason[] =
        {
            "ok", "not an iNES image", "too large", "unsupported mapper"
        };
        static const char *reason_cn[] =
        {
            "ok", "不是 NES 文件", "文件过大", "Mapper 不支持"
        };

        PRINT_LOG("[NES ] %s rejected: %s\r\n", utf8, reason[(int)err]);
        s_fail_reason = reason_cn[(int)err];
        return -1;
    }

    PRINT_LOG("[NES ] %s loaded, %lu B, mapper %u (%s)\r\n",
           utf8, (unsigned long)read,
           (unsigned)nes_mapper_number(), nes_mapper_name());

    return 0;
}

static int load_rom(int index)
{
    if ((index < 0) || (index >= s_rom_count))
    {
        s_fail_reason = "序号越界";
        return -1;
    }
    return load_rom_named(s_names[index]);
}

/*----------------------------------------------------------------------------
 *  Emulator output
 *--------------------------------------------------------------------------*/

static void blit_frame(void)
{
    const uint8_t *fb = nes_framebuffer();
    uint16_t       y;

    for (y = 0U; y < NES_SCREEN_H; y += NES_BAND_LINES)
    {
        uint16_t  rows = NES_BAND_LINES;
        uint16_t *dst  = s_band;
        uint16_t  row;

        if ((uint16_t)(y + rows) > (uint16_t)NES_SCREEN_H)
        {
            rows = (uint16_t)(NES_SCREEN_H - y);
        }

        for (row = 0U; row < rows; row++)
        {
            const uint8_t *src = &fb[((uint32_t)(y + row) * NES_SCREEN_W) +
                                     NES_CROP_X];
            uint16_t       x;

            for (x = 0U; x < UI_W; x++)
            {
                *dst = nes_palette_rgb565[src[x] & 0x3FU];
                dst++;
            }
        }

        LCD_CopyBuffer(0U, y, UI_W, rows, s_band);
    }
}

/** Translate the virtual key layer into the NES pad byte. */
static void feed_pad(void)
{
    uint32_t m   = bsp_key_state();
    uint8_t  pad = 0U;

    /* KEY_SELECT is reserved as the "leave the game" key (see nes_key), so it
     * is deliberately NOT forwarded to the NES pad. */
    if ((m & ((uint32_t)1U << KEY_A))      != 0U) { pad |= NES_PAD_A;      }
    if ((m & ((uint32_t)1U << KEY_B))      != 0U) { pad |= NES_PAD_B;      }
    if ((m & ((uint32_t)1U << KEY_START))  != 0U) { pad |= NES_PAD_START;  }
    if ((m & ((uint32_t)1U << KEY_UP))     != 0U) { pad |= NES_PAD_UP;     }
    if ((m & ((uint32_t)1U << KEY_DOWN))   != 0U) { pad |= NES_PAD_DOWN;   }
    if ((m & ((uint32_t)1U << KEY_LEFT))   != 0U) { pad |= NES_PAD_LEFT;   }
    if ((m & ((uint32_t)1U << KEY_RIGHT))  != 0U) { pad |= NES_PAD_RIGHT;  }

    nes_set_pad(0U, pad);
}

/*----------------------------------------------------------------------------
 *  Browser
 *--------------------------------------------------------------------------*/

static void start_game(void);

/** sd_browser told us the user confirmed a file. */
static void on_pick(int index, const char *name_gbk, const char *name_utf8,
                    void *ctx)
{
    (void)index;
    (void)ctx;

    if (load_rom_named(name_gbk) == 0)
    {
        start_game();
        return;
    }

    PRINT_LOG("[NES ] cannot load %s: %s\r\n",
           (name_utf8 != NULL) ? name_utf8 : "?", s_fail_reason);

    sd_browser_show_error(s_browser, "无法加载", s_fail_reason);
}

static void open_browser(lv_obj_t *root)
{
    s_browser = sd_browser_create(root, NES_DIR, "NES 模拟器",
                                  NES_ROWS_VISIBLE, NES_ROW_H,
                                  on_pick, NULL, NULL);
}

/*----------------------------------------------------------------------------
 *  Page callbacks
 *--------------------------------------------------------------------------*/

static void nes_enter(lv_obj_t *root)
{
    s_root  = root;
    s_state = NES_STATE_BROWSER;

    /* Grab the DTCM machine-state + D2 ROM arena now; they are freed again in
     * nes_exit so the memory is available to other apps while we are not here. */
    if (nes_open() != 0)
    {
        PRINT_LOG("[NES ] cannot allocate emulator memory\r\n");
    }

    find_roms();                    /* keeps the console list in sync */
    open_browser(root);
}

static void nes_exit(void)
{
    s_state = NES_STATE_BROWSER;
    sd_browser_destroy(s_browser);
    s_browser = NULL;
    s_root    = NULL;
    bsp_key_release_all();
    nes_close();                    /* release DTCM + D2 back to sram_pool */
}

/** Leave the game and rebuild the browser without bouncing to the menu. */
static void stop_game(void)
{
    s_state = NES_STATE_BROWSER;
    nes_set_pad(0U, 0U);
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

    PRINT_LOG("[NES ] stopped after %lu frames\r\n",
           (unsigned long)nes_frame_count());
}

static void start_game(void)
{
    s_state           = NES_STATE_RUNNING;
    s_last_frame_tick = HAL_GetTick();
    s_fps_window      = s_last_frame_tick;
    s_fps_frames      = 0U;

    LCD_SetBackColor(LCD_BLACK);
    LCD_Clear();

    PRINT_LOG("[NES ] running - 按 SELECT / BACK / MENU 退出\r\n");
}

static int nes_key(key_id_t id, key_edge_t edge)
{
    if (s_state == NES_STATE_RUNNING)
    {
        /* The pad is sampled from the level state each frame, so nothing to do
         * here except catch the keys that mean "get me out".  SELECT is the
         * primary exit (this build); BACK and MENU remain as alternates. */
        if ((edge == KEY_EV_DOWN) &&
            ((id == KEY_BACK) || (id == KEY_MENU) || (id == KEY_SELECT)))
        {
            stop_game();
        }
        return 1;                               /* never bubble up while running */
    }

    if (edge != KEY_EV_DOWN)
    {
        return 1;
    }

    /* The "cannot load" overlay swallows everything until it is dismissed. */
    if (sd_browser_is_error(s_browser) != 0)
    {
        if ((id == KEY_SELECT) || (id == KEY_B) || (id == KEY_BACK) ||
            (id == KEY_A) || (id == KEY_OK))
        {
            sd_browser_hide_error(s_browser);
        }
        return 1;
    }

    /* UP/DOWN move the highlight, A/OK/START confirm (-> on_pick). */
    if (sd_browser_key(s_browser, id, edge) != 0)
    {
        return 1;
    }

    if (id == KEY_SELECT)
    {
        /* SELECT leaves the NES page and returns to the main menu. */
        app_menu_back();
        return 1;
    }

    /* B / BACK fall through so the menu closes the page. */
    return 0;
}

static void nes_tick(void)
{
    uint32_t now;

    /* A console "rom load N" arrived while the browser was up. */
    if (s_pending_load >= 0)
    {
        int index = s_pending_load;

        s_pending_load = -1;

        if (s_state == NES_STATE_RUNNING)
        {
            stop_game();
        }
        if (load_rom(index) == 0)
        {
            start_game();
        }
        else
        {
            sd_browser_show_error(s_browser, "无法加载", s_fail_reason);
        }
    }

    if (s_state != NES_STATE_RUNNING)
    {
        return;
    }

    now = HAL_GetTick();

    /* Cap at 60 Hz.  The emulator is usually the slower one, but a small ROM
     * on a 480 MHz M7 can outrun the real console and the game would speed up. */
    if ((now - s_last_frame_tick) < 16U)
    {
        return;
    }
    s_last_frame_tick = now;

    feed_pad();
    nes_run_frame();
    blit_frame();

    s_fps_frames++;
    if ((now - s_fps_window) >= 1000U)
    {
        s_fps        = s_fps_frames;
        s_fps_frames = 0U;
        s_fps_window = now;
    }
}

static int nes_wants_display(void)
{
    return (s_state == NES_STATE_RUNNING) ? 1 : 0;
}

static const app_page_t s_page_nes =
{
    .title         = "NES 模拟器",
    .hint          = "SD 根目录选文件运行",
    .cmd           = "nes",
    .full_screen   = 0U,
    .icon          = &icon_nes,
    .on_enter      = nes_enter,
    .on_exit       = nes_exit,
    .on_key        = nes_key,
    .on_tick       = nes_tick,
    .wants_display = nes_wants_display
};

const app_page_t *page_nes_get(void)
{
    return &s_page_nes;
}

/*----------------------------------------------------------------------------
 *  Console hooks (used by app_cmd.c)
 *--------------------------------------------------------------------------*/

int page_nes_rom_count(void)
{
    return s_rom_count;
}

const char *page_nes_rom_name(int index)
{
    if ((index < 0) || (index >= s_rom_count))
    {
        return NULL;
    }
    return s_names[index];
}

const char *page_nes_dir(void)
{
    return s_dir;
}

void page_nes_rescan(void)
{
    find_roms();
}

/** Queue a load; the actual file I/O happens on the page tick so the console
 *  never blocks and LVGL is not touched from a half-parsed command. */
void page_nes_request_load(int index)
{
    s_pending_load = index;
}

int page_nes_is_running(void)
{
    return (s_state == NES_STATE_RUNNING) ? 1 : 0;
}

uint32_t page_nes_fps(void)
{
    return s_fps;
}

void page_nes_stop(void)
{
    if (s_state == NES_STATE_RUNNING)
    {
        stop_game();
    }
}
