/**
  ******************************************************************************
  * @file    app_cmd.c
  * @brief   Line-oriented console parser: the board's only input device.
  *
  *  The LXB743ZI-P1 has no push-buttons, so every UI action arrives as text on
  *  USART1 (ST-Link VCP) or the USB CDC port.  Both links land in the same RX
  *  ring (bsp_console.c), so this parser does not care which cable is used.
  *
  *  Design notes
  *  ------------
  *  - A command is a CR- or LF-terminated line, at most CMD_LINE_MAX bytes.
  *    Overlong lines are truncated and reported instead of silently wrapping
  *    into the next command, which would produce very confusing behaviour on a
  *    flaky cable.
  *  - Parsing is destructive tokenisation of the local buffer (no strtok_r on
  *    newlib-nano, and strtok's static state is a landmine in an ISR-adjacent
  *    code path), so the line buffer is private and reset every command.
  *  - Every command answers with exactly one line beginning with "OK" or
  *    "ERR", followed by optional payload lines.  That makes the Python test
  *    script (scripts/serial_test.py) trivial: read until a line starts with
  *    OK/ERR.
  *  - Echo is on by default so a human on a terminal sees what they type; the
  *    test script turns it off with "echo off" to keep its parsing simple.
  ******************************************************************************
  */
#include "app_cmd.h"
#include "app_main.h"
#include "app_page.h"
#include "bsp_console.h"
#include "bsp_key.h"
#include "drv_rtc.h"
#include "main.h"
#include "nes.h"
#include "sram_pool.h"
#include "drv_camera.h" /* g_cam_* counters + page_camera_* hooks */
#include "gbk_conv.h"   /* GBK(8.3 short names on the card) -> UTF-8 for the console */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define CMD_LINE_MAX        96U
#define CMD_ARG_MAX         6U

/** Reply prefix constants - keep these stable, the test script matches them. */
#define RSP_OK              "OK"
#define RSP_ERR             "ERR"

static char     s_line[CMD_LINE_MAX + 1U];
static uint32_t s_len;
static uint8_t  s_overflow;
static uint8_t  s_echo = 1U;

/*----------------------------------------------------------------------------
 *  Output helpers
 *--------------------------------------------------------------------------*/

static void reply(const char *fmt, ...)
{
    static char buf[160];
    va_list     ap;
    int         n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf) - 3U, fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        return;
    }

    if ((uint32_t)n > (sizeof(buf) - 3U))
    {
        n = (int)(sizeof(buf) - 3U);
    }

    buf[n]      = '\r';
    buf[n + 1]  = '\n';
    buf[n + 2]  = '\0';

    bsp_console_puts(buf);
}

static void ok(const char *fmt, ...)
{
    static char buf[128];
    va_list     ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    reply("%s %s", RSP_OK, buf);
}

static void err(const char *fmt, ...)
{
    static char buf[128];
    va_list     ap;

    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    reply("%s %s", RSP_ERR, buf);
}

/*----------------------------------------------------------------------------
 *  Small string helpers (newlib-nano friendly)
 *--------------------------------------------------------------------------*/

static int str_ieq(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0'))
    {
        char ca = *a;
        char cb = *b;

        if ((ca >= 'A') && (ca <= 'Z')) { ca = (char)(ca + 32); }
        if ((cb >= 'A') && (cb <= 'Z')) { cb = (char)(cb + 32); }

        if (ca != cb)
        {
            return 0;
        }
        a++;
        b++;
    }

    return ((*a == '\0') && (*b == '\0')) ? 1 : 0;
}

static void str_lower(char *s)
{
    while (*s != '\0')
    {
        if ((*s >= 'A') && (*s <= 'Z'))
        {
            *s = (char)(*s + 32);
        }
        s++;
    }
}

/**
  * @brief  Split line into whitespace-separated tokens, in place.
  * @return token count (0 for an empty line)
  */
static uint32_t tokenise(char *line, char *argv[], uint32_t max)
{
    uint32_t argc = 0U;
    char    *p    = line;

    while ((*p != '\0') && (argc < max))
    {
        while ((*p == ' ') || (*p == '\t'))
        {
            p++;
        }

        if (*p == '\0')
        {
            break;
        }

        argv[argc++] = p;

        while ((*p != '\0') && (*p != ' ') && (*p != '\t'))
        {
            p++;
        }

        if (*p != '\0')
        {
            *p = '\0';
            p++;
        }
    }

    return argc;
}

/*----------------------------------------------------------------------------
 *  Commands
 *--------------------------------------------------------------------------*/

static void cmd_help(void)
{
    int i;

    reply("---- console commands ----------------------------");
    reply("help                 this list");
    reply("status               firmware / UI / emulator state");
    reply("pages                list registered pages");
    reply("menu                 close the current page");
    reply("open <page|index>    open a page by cmd name or number");
    reply("back                 same as menu");
    reply("sel <index>          move the menu highlight");
    reply("key <name> [ms]      tap a key (default %u ms)", KEY_TAP_DEFAULT_MS);
    reply("down <name>          press and hold");
    reply("up <name>            release");
    reply("release              release every key");
    reply("keys                 list key names / held mask");
    reply("rom list             list *.nes on the card");
    reply("rom load <index>     load and start a ROM");
    reply("rom stop             leave the emulator");
    reply("rom info             emulator state");
    reply("img list             list *.bmp / *.jpg on the card");
    reply("img show <index>     decode and display an image");
    reply("img close            leave the viewer");
    reply("img info             viewer state");
    reply("txt list             list *.txt on the card");
    reply("txt open <index>     open a .txt and start reading");
    reply("txt close            leave the reader");
    reply("txt info             reader state");
    reply("txt seed [name]      write a multi-page test fixture to the card");
    reply("cam open             open the 96x96 live preview page");
    reply("cam stop             leave the camera page");
    reply("cam info             preview state / fps / overruns");
    reply("time                 read the RTC");
    reply("time <Y-M-D> <h:m:s> set the RTC");
    reply("echo on|off          local echo of typed characters");
    reply("sram info            per-region free bytes + integrity");
    reply("sram check [dtcm|d2] verify allocator invariants");
    reply("sram alloc <r> <sz>  allocate from the pool (debug)");
    reply("sram free  <r> <addr> free a block returned by alloc");
    reply("sram stress <r> <n> [seed] randomized fragmentation test");
    reply("reset                reboot the MCU");
    reply("--------------------------------------------------");

    reply("pages:");
    for (i = 0; i < app_menu_count(); i++)
    {
        const app_page_t *p = app_menu_page(i);
        reply("  %d %-10s %s", i, (p->cmd != NULL) ? p->cmd : "-", p->title);
    }

    ok("help");
}

static void cmd_pages(void)
{
    int i;
    int cur = app_menu_current();

    for (i = 0; i < app_menu_count(); i++)
    {
        const app_page_t *p = app_menu_page(i);
        reply("%c %d %-10s %s",
              (i == cur) ? '*' : ' ',
              i,
              (p->cmd != NULL) ? p->cmd : "-",
              p->title);
    }

    ok("pages %d", app_menu_count());
}

static void cmd_status(void)
{
    int               cur  = app_menu_current();
    const app_page_t *page = (cur >= 0) ? app_menu_page(cur) : NULL;

    reply("fw     : %s %s", APP_FW_NAME, APP_FW_VERSION);
    reply("build  : %s %s", __DATE__, __TIME__);
    reply("sysclk : %lu Hz", (unsigned long)HAL_RCC_GetSysClockFreq());
    reply("uptime : %lu ms", (unsigned long)HAL_GetTick());
    reply("clock  : %s",
          (g_clock_source == CLOCK_SRC_HSE_XTAL) ? "HSE 25MHz" : "HSI (fallback)");
    reply("console: uart%s", bsp_console_usb_ready() ? "+usb" : " only");
    reply("cache  : %s%s",
          (SCB->CCR & SCB_CCR_IC_Msk) ? "I" : "off",
          (SCB->CCR & SCB_CCR_DC_Msk) ? "+D" : "");
    reply("sram  dtcm: %u/%u B free",
          (unsigned)sram_free_bytes(SRAM_REGION_DTCM),
          (unsigned)sram_total_bytes(SRAM_REGION_DTCM));
    reply("sram  d2  : %u/%u B free",
          (unsigned)sram_free_bytes(SRAM_REGION_D2),
          (unsigned)sram_total_bytes(SRAM_REGION_D2));
    reply("view   : %s%s",
          (page != NULL) ? page->cmd : "menu",
          app_menu_is_full_screen() ? " (fullscreen)" : "");
    reply("sel    : %d", app_menu_selection());
    reply("keys   : 0x%08lX", (unsigned long)bsp_key_state());
    reply("nes    : %s, roms %d, fps %lu",
          page_nes_is_running() ? "running" : "idle",
          page_nes_rom_count(),
          (unsigned long)page_nes_fps());
    reply("image  : %s, files %d",
          page_image_is_viewing() ? "viewing" : "idle",
          page_image_count());
    reply("txt    : %s, files %d",
          page_txt_is_reading() ? "reading" : "idle",
          page_txt_count());

    {
        char cam_buf[64];
        page_camera_info(cam_buf, (int)sizeof(cam_buf));
        reply("cam    : %s", cam_buf);
    }

    ok("status");
}

static void cmd_open(uint32_t argc, char *argv[])
{
    int index;

    if (argc < 2U)
    {
        err("open needs a page name or index");
        return;
    }

    /* Numeric argument -> index, otherwise the page's cmd handle. */
    if ((argv[1][0] >= '0') && (argv[1][0] <= '9'))
    {
        index = atoi(argv[1]);
    }
    else
    {
        index = -1;
        {
            int i;
            for (i = 0; i < app_menu_count(); i++)
            {
                const app_page_t *p = app_menu_page(i);
                if ((p->cmd != NULL) && str_ieq(p->cmd, argv[1]))
                {
                    index = i;
                    break;
                }
            }
        }
    }

    if ((index < 0) || (index >= app_menu_count()))
    {
        err("no such page: %s", argv[1]);
        return;
    }

    if (app_menu_open_index(index) != 0)
    {
        err("open failed");
        return;
    }

    ok("open %d %s", index, app_menu_page(index)->cmd);
}

static void cmd_key(uint32_t argc, char *argv[], key_edge_t edge, int tap)
{
    int      id;
    uint16_t hold = 0U;

    if (argc < 2U)
    {
        err("missing key name");
        return;
    }

    str_lower(argv[1]);
    id = bsp_key_from_name(argv[1]);

    if (id < 0)
    {
        err("unknown key: %s", argv[1]);
        return;
    }

    if (tap != 0)
    {
        if (argc >= 3U)
        {
            int ms = atoi(argv[2]);
            if ((ms > 0) && (ms < 5000))
            {
                hold = (uint16_t)ms;
            }
        }
        bsp_key_tap((key_id_t)id, hold);
        ok("key %s", bsp_key_name((key_id_t)id));
    }
    else
    {
        bsp_key_inject((key_id_t)id, edge);
        ok("%s %s", (edge == KEY_EV_DOWN) ? "down" : "up",
                    bsp_key_name((key_id_t)id));
    }
}

static void cmd_keys(void)
{
    uint32_t mask = bsp_key_state();
    int      i;

    for (i = 0; i < (int)KEY_COUNT; i++)
    {
        reply("%-7s %s", bsp_key_name((key_id_t)i),
              ((mask >> i) & 1U) ? "DOWN" : "-");
    }

    ok("keys 0x%08lX", (unsigned long)mask);
}

static void cmd_rom(uint32_t argc, char *argv[])
{
    if (argc < 2U)
    {
        err("rom needs list|load|stop|info");
        return;
    }

    str_lower(argv[1]);

    if (str_ieq(argv[1], "list"))
    {
        int n;
        int i;

        page_nes_rescan();
        n = page_nes_rom_count();

        reply("dir: %s", page_nes_dir());
        for (i = 0; i < n; i++)
        {
            /* s_names[] keeps the GBK bytes from the card's 8.3 entry (needed
             * for f_open byte matching); transcode to UTF-8 only for display.
             * NES_NAME_LEN is 40, so 3 bytes/char + slack fits comfortably. */
            char utf8[160];
            gbk_to_utf8(page_nes_rom_name(i), utf8, (int)sizeof(utf8));
            reply("%2d %s", i, utf8);
        }

        ok("rom list %d", n);
        return;
    }

    if (str_ieq(argv[1], "load"))
    {
        int index;

        if (argc < 3U)
        {
            err("rom load needs an index");
            return;
        }

        index = atoi(argv[2]);

        if ((index < 0) || (index >= page_nes_rom_count()))
        {
            err("rom index out of range (have %d)", page_nes_rom_count());
            return;
        }

        /* The load itself happens in the page tick: reading a 256 KB file off
         * the card takes long enough that doing it here would stall the
         * console and make the reply arrive after the first video frame. */
        if (app_menu_open_cmd("nes") != 0)
        {
            err("cannot open the nes page");
            return;
        }

        page_nes_request_load(index);
        ok("rom load %d %s", index, page_nes_rom_name(index));
        return;
    }

    if (str_ieq(argv[1], "stop"))
    {
        page_nes_stop();
        ok("rom stop");
        return;
    }

    if (str_ieq(argv[1], "info"))
    {
        reply("state  : %s", page_nes_is_running() ? "running" : "idle");
        reply("mapper : %d (%s)", nes_mapper_number(), nes_mapper_name());
        reply("frames : %lu", (unsigned long)nes_frame_count());
        reply("fps    : %lu", (unsigned long)page_nes_fps());
        reply("pad    : 0x%02X", (unsigned)(bsp_key_state() & 0xFFU));
        ok("rom info");
        return;
    }

    err("rom: unknown sub-command %s", argv[1]);
}

static void cmd_img(uint32_t argc, char *argv[])
{
    if (argc < 2U)
    {
        err("img needs list|show|close|info");
        return;
    }

    if (str_ieq(argv[1], "list"))
    {
        int n;
        int i;

        page_image_rescan();
        n = page_image_count();

        reply("dir: %s", page_image_dir());
        for (i = 0; i < n; i++)
        {
            /* Same story as the ROM list: the card gives GBK 8.3 names, the
             * console speaks UTF-8, and the GBK bytes are what f_open wants. */
            char utf8[160];
            gbk_to_utf8(page_image_name(i), utf8, (int)sizeof(utf8));
            reply("%2d %s", i, utf8);
        }

        ok("img list %d", n);
        return;
    }

    if (str_ieq(argv[1], "show"))
    {
        int index;

        if (argc < 3U)
        {
            err("img show needs an index");
            return;
        }

        index = atoi(argv[2]);

        if ((index < 0) || (index >= page_image_count()))
        {
            err("img index out of range (have %d)", page_image_count());
            return;
        }

        /* Decoding a JPEG off the card takes a while: queue it for the page
         * tick so the parser stays responsive, exactly like "rom load". */
        if (app_menu_open_cmd("image") != 0)
        {
            err("cannot open the image page");
            return;
        }

        page_image_request_show(index);
        ok("img show %d %s", index, page_image_name(index));
        return;
    }

    if (str_ieq(argv[1], "close"))
    {
        page_image_close();
        ok("img close");
        return;
    }

    if (str_ieq(argv[1], "info"))
    {
        char line[200];

        page_image_info(line, (int)sizeof(line));

        reply("state  : %s", page_image_is_viewing() ? "viewing" : "idle");
        reply("files  : %d", page_image_count());
        reply("shown  : %s", line);
        ok("img info");
        return;
    }

    err("img: unknown sub-command %s", argv[1]);
}

static void cmd_txt(uint32_t argc, char *argv[])
{
    if (argc < 2U)
    {
        err("txt needs list|open|close|info");
        return;
    }

    if (str_ieq(argv[1], "list"))
    {
        int n;
        int i;

        page_txt_rescan();
        n = page_txt_count();

        reply("dir: %s", page_txt_dir());
        for (i = 0; i < n; i++)
        {
            char utf8[160];
            gbk_to_utf8(page_txt_name(i), utf8, (int)sizeof(utf8));
            reply("%2d %s", i, utf8);
        }

        ok("txt list %d", n);
        return;
    }

    if (str_ieq(argv[1], "open"))
    {
        int index;

        if (argc < 3U)
        {
            err("txt open needs an index");
            return;
        }

        index = atoi(argv[2]);

        if ((index < 0) || (index >= page_txt_count()))
        {
            err("txt index out of range (have %d)", page_txt_count());
            return;
        }

        /* Open the page (so the reader view exists) and queue the load for the
         * page tick - the same pattern as "rom load" / "img show". */
        if (app_menu_open_cmd("txt") != 0)
        {
            err("cannot open the txt page");
            return;
        }

        page_txt_request_open(index);
        ok("txt open %d %s", index, page_txt_name(index));
        return;
    }

    if (str_ieq(argv[1], "close"))
    {
        page_txt_close();
        ok("txt close");
        return;
    }

    if (str_ieq(argv[1], "seed"))
    {
        const char *nm = (argc >= 3U) ? argv[2] : "SEED.TXT";

        if (page_txt_seed(nm) == 0)
        {
            ok("txt seed %s", nm);
        }
        else
        {
            err("txt seed failed (card write error?)");
        }
        return;
    }

    if (str_ieq(argv[1], "cddir"))
    {
        if (argc < 3U)
        {
            err("txt cddir needs a sub-directory");
            return;
        }
        page_txt_cddir(argv[2]);
        ok("txt cddir %s", argv[2]);
        return;
    }

    if (str_ieq(argv[1], "info"))
    {
        char line[200];

        page_txt_info(line, (int)sizeof(line));

        reply("state  : %s", page_txt_is_reading() ? "reading" : "idle");
        reply("files  : %d", page_txt_count());
        reply("file   : %s", line);
        ok("txt info");
        return;
    }

    if (str_ieq(argv[1], "sel"))
    {
        int idx = page_txt_browser_sel();

        if (idx < 0)
        {
            reply("sel    : n/a (not in the file list)");
        }
        else
        {
            char nm[200];

            page_txt_browser_name(nm, (int)sizeof(nm));
            reply("sel    : %d (%s)", idx, nm);
        }
        ok("txt sel");
        return;
    }

    err("txt: unknown sub-command %s", argv[1]);
}

static void cmd_cam(uint32_t argc, char *argv[])
{
    if (argc < 2U)
    {
        err("cam needs open|stop|info|state");
        return;
    }

    str_lower(argv[1]);

    if (str_ieq(argv[1], "open"))
    {
        /* The page owns the capture buffers (allocated from the shared
         * sram_pool at on_enter and freed at on_exit), so we just navigate
         * to it like any other full_screen page. */
        if (app_menu_open_cmd("camera") != 0)
        {
            err("cannot open the camera page");
            return;
        }
        ok("cam open");
        return;
    }

    if (str_ieq(argv[1], "stop"))
    {
        page_camera_stop();
        ok("cam stop");
        return;
    }

    if (str_ieq(argv[1], "info") || str_ieq(argv[1], "state"))
    {
        char buf[96];
        page_camera_info(buf, (int)sizeof(buf));
        reply("state  : %s", buf);
        reply("fps    : %lu", (unsigned long)g_cam_fps);
        reply("frames : %lu", (unsigned long)g_cam_frames);
        reply("overruns: %lu", (unsigned long)g_cam_overruns);
        ok("cam info");
        return;
    }

    err("cam: unknown sub-command %s", argv[1]);
}

static void cmd_time(uint32_t argc, char *argv[])
{
    rtc_datetime_t dt;

    if (argc == 1U)
    {
        if (drv_rtc_get(&dt) != RT_OK)
        {
            err("rtc unavailable");
            return;
        }
        ok("time %04u-%02u-%02u %02u:%02u:%02u",
           (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.day,
           (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
        return;
    }

    if (argc < 3U)
    {
        err("time <YYYY-MM-DD> <hh:mm:ss>");
        return;
    }

    {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;

        if (sscanf(argv[1], "%d-%d-%d", &y, &mo, &d) != 3)
        {
            err("bad date, want YYYY-MM-DD");
            return;
        }
        if (sscanf(argv[2], "%d:%d:%d", &h, &mi, &s) != 3)
        {
            err("bad time, want hh:mm:ss");
            return;
        }

        dt.year    = (uint16_t)y;
        dt.month   = (uint8_t)mo;
        dt.day     = (uint8_t)d;
        dt.hour    = (uint8_t)h;
        dt.minute  = (uint8_t)mi;
        dt.second  = (uint8_t)s;
        dt.weekday = 0U;            /* recomputed by the driver */

        if (drv_rtc_set(&dt) != RT_OK)
        {
            err("rtc write failed");
            return;
        }

        ok("time set");
    }
}

/*----------------------------------------------------------------------------
 *  sram - dynamic pool debugging / stress
 *--------------------------------------------------------------------------*/

static int sram_parse_region(const char *s, sram_region_t *out)
{
    char buf[8];
    int  i = 0;

    while ((*s != '\0') && (i < (int)sizeof(buf) - 1) && (*s != ' '))
    {
        char c = *s;
        if ((c >= 'A') && (c <= 'Z')) { c = (char)(c + 32); }
        buf[i++] = c;
        s++;
    }
    buf[i] = '\0';

    if (str_ieq(buf, "dtcm") || str_ieq(buf, "0"))  { *out = SRAM_REGION_DTCM; return 1; }
    if (str_ieq(buf, "d2")   || str_ieq(buf, "1"))  { *out = SRAM_REGION_D2;   return 1; }
    return 0;
}

static const char *sram_region_name(sram_region_t r)
{
    return (r == SRAM_REGION_DTCM) ? "dtcm" : "d2";
}

static void cmd_sram(uint32_t argc, char *argv[])
{
    if (argc < 2U)
    {
        err("sram needs info|check|alloc|free|stress");
        return;
    }

    str_lower(argv[1]);

    if (str_ieq(argv[1], "info"))
    {
        for (int r = 0; r < (int)SRAM_REGION_COUNT; r++)
        {
            int      okf  = 1;
            size_t   f    = sram_check((sram_region_t)r, &okf);
            uint32_t tot  = (uint32_t)sram_total_bytes((sram_region_t)r);
            reply("%-5s free %u/%u B, integrity %s",
                  sram_region_name((sram_region_t)r),
                  (unsigned)f, tot, okf ? "ok" : "BAD");
        }
        ok("sram info");
        return;
    }

    if (str_ieq(argv[1], "check"))
    {
        sram_region_t reg = SRAM_REGION_D2;
        if (argc >= 3U)
        {
            if (!sram_parse_region(argv[2], &reg))
            {
                err("sram check: bad region");
                return;
            }
        }
        int    okf = 1;
        size_t f   = sram_check(reg, &okf);
        ok("sram check %s free %u integrity %s",
           sram_region_name(reg), (unsigned)f, okf ? "ok" : "BAD");
        return;
    }

    if (str_ieq(argv[1], "alloc"))
    {
        sram_region_t reg;
        uint32_t      sz;

        if (argc < 4U)
        {
            err("sram alloc <dtcm|d2> <size>");
            return;
        }
        if (!sram_parse_region(argv[2], &reg))
        {
            err("sram alloc: bad region");
            return;
        }
        sz = (uint32_t)strtoul(argv[3], NULL, 0);
        void *p = sram_alloc(reg, sz, 8U);
        if (p != NULL)
        {
            ok("sram alloc %08lX", (unsigned long)(uintptr_t)p);
        }
        else
        {
            err("sram alloc failed (size %u)", (unsigned)sz);
        }
        return;
    }

    if (str_ieq(argv[1], "free"))
    {
        sram_region_t reg;
        void         *p;

        if (argc < 4U)
        {
            err("sram free <dtcm|d2> <addr>");
            return;
        }
        if (!sram_parse_region(argv[2], &reg))
        {
            err("sram free: bad region");
            return;
        }
        p = (void *)(uintptr_t)strtoul(argv[3], NULL, 0);
        sram_free(reg, p);
        ok("sram free");
        return;
    }

    if (str_ieq(argv[1], "stress"))
    {
        sram_region_t reg;
        uint32_t      iters;
        uint32_t      seed = 0U;

        if (argc < 4U)
        {
            err("sram stress <dtcm|d2> <iters> [seed]");
            return;
        }
        if (!sram_parse_region(argv[2], &reg))
        {
            err("sram stress: bad region");
            return;
        }
        iters = (uint32_t)strtoul(argv[3], NULL, 0);
        if (argc >= 5U)
        {
            seed = (uint32_t)strtoul(argv[4], NULL, 0);
        }
        int rc = sram_stress_test(reg, iters, seed);
        if (rc == 0)
        {
            ok("sram stress PASS iters %u", (unsigned)iters);
        }
        else
        {
            err("sram stress FAIL code %d iters %u", rc, (unsigned)iters);
        }
        return;
    }

    err("sram: unknown sub-command %s", argv[1]);
}

/**
  * @brief  Execute one complete line.
  */
static void dispatch(char *line)
{
    char    *argv[CMD_ARG_MAX];
    uint32_t argc = tokenise(line, argv, CMD_ARG_MAX);

    if (argc == 0U)
    {
        return;                     /* bare Enter: no reply, no noise */
    }

    str_lower(argv[0]);

    if (str_ieq(argv[0], "help") || str_ieq(argv[0], "?"))
    {
        cmd_help();
    }
    else if (str_ieq(argv[0], "status"))
    {
        cmd_status();
    }
    else if (str_ieq(argv[0], "pages"))
    {
        cmd_pages();
    }
    else if (str_ieq(argv[0], "menu") || str_ieq(argv[0], "back"))
    {
        app_menu_back();
        ok("menu");
    }
    else if (str_ieq(argv[0], "open"))
    {
        cmd_open(argc, argv);
    }
    else if (str_ieq(argv[0], "sel"))
    {
        if (argc < 2U)
        {
            err("sel needs an index");
        }
        else
        {
            app_menu_select(atoi(argv[1]));
            ok("sel %d", app_menu_selection());
        }
    }
    else if (str_ieq(argv[0], "key"))
    {
        cmd_key(argc, argv, KEY_EV_DOWN, 1);
    }
    else if (str_ieq(argv[0], "down"))
    {
        cmd_key(argc, argv, KEY_EV_DOWN, 0);
    }
    else if (str_ieq(argv[0], "up"))
    {
        cmd_key(argc, argv, KEY_EV_UP, 0);
    }
    else if (str_ieq(argv[0], "release"))
    {
        bsp_key_release_all();
        ok("release");
    }
    else if (str_ieq(argv[0], "keys"))
    {
        cmd_keys();
    }
    else if (str_ieq(argv[0], "rom"))
    {
        cmd_rom(argc, argv);
    }
    else if (str_ieq(argv[0], "img"))
    {
        cmd_img(argc, argv);
    }
    else if (str_ieq(argv[0], "txt"))
    {
        cmd_txt(argc, argv);
    }
    else if (str_ieq(argv[0], "cam"))
    {
        cmd_cam(argc, argv);
    }
    else if (str_ieq(argv[0], "time"))
    {
        cmd_time(argc, argv);
    }
    else if (str_ieq(argv[0], "sram"))
    {
        cmd_sram(argc, argv);
    }
    else if (str_ieq(argv[0], "echo"))
    {
        if ((argc >= 2U) && str_ieq(argv[1], "off"))
        {
            s_echo = 0U;
        }
        else if ((argc >= 2U) && str_ieq(argv[1], "on"))
        {
            s_echo = 1U;
        }
        ok("echo %s", (s_echo != 0U) ? "on" : "off");
    }
    else if (str_ieq(argv[0], "reset"))
    {
        ok("reset");
        HAL_Delay(50);              /* let the reply drain out of both FIFOs */
        NVIC_SystemReset();
    }
    else
    {
        err("unknown command '%s', try help", argv[0]);
    }
}

/*----------------------------------------------------------------------------
 *  Public
 *--------------------------------------------------------------------------*/

void app_cmd_init(void)
{
    s_len      = 0U;
    s_overflow = 0U;
    s_echo     = 1U;

    reply("");
    reply("%s %s ready - type 'help'", APP_FW_NAME, APP_FW_VERSION);
}

void app_cmd_task(void)
{
    int c;

    /* Bounded per pass: a host that pastes a whole script must not be able to
     * starve the video loop.  32 bytes is one command line's worth. */
    uint32_t budget = 32U;

    while ((budget-- > 0U) && ((c = bsp_console_getc()) >= 0))
    {
        char ch = (char)c;

        if ((ch == '\r') || (ch == '\n'))
        {
            if (s_echo != 0U)
            {
                bsp_console_puts("\r\n");
            }

            if (s_overflow != 0U)
            {
                s_overflow = 0U;
                s_len      = 0U;
                err("line too long (max %u)", (unsigned)CMD_LINE_MAX);
                continue;
            }

            s_line[s_len] = '\0';
            s_len         = 0U;
            dispatch(s_line);
            continue;
        }

        /* Backspace / DEL from an interactive terminal. */
        if ((ch == '\b') || (ch == 0x7F))
        {
            if (s_len > 0U)
            {
                s_len--;
                if (s_echo != 0U)
                {
                    bsp_console_puts("\b \b");
                }
            }
            continue;
        }

        /* Ctrl-C: abandon the line and release the pad, which is the panic
         * button when a "down" command left a key stuck. */
        if (ch == 0x03)
        {
            s_len      = 0U;
            s_overflow = 0U;
            bsp_key_release_all();
            reply("");
            ok("abort");
            continue;
        }

        if ((ch < 0x20) || (ch > 0x7E))
        {
            continue;               /* ignore stray control bytes */
        }

        if (s_len >= CMD_LINE_MAX)
        {
            s_overflow = 1U;
            continue;
        }

        s_line[s_len++] = ch;

        if (s_echo != 0U)
        {
            char e[2] = { ch, '\0' };
            bsp_console_puts(e);
        }
    }
}

int app_cmd_echo(void)
{
    return (s_echo != 0U) ? 1 : 0;
}
