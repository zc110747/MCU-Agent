/**
  ******************************************************************************
  * @file    shell_cmds.c
  * @brief   Portable Zephyr shell command set (serial console).
  *
  *  This module is a drop-in, hardware-independent shell command set.  It only
  *  uses the public Zephyr shell API (SHELL_CMD_REGISTER / shell_print / ...)
  *  plus a couple of project helpers for the demo commands, so the command
  *  registration pattern itself can be copied verbatim into any Zephyr app.
  *
  *  Usage on the debug serial (USART1 PA9/PA10, 115200 8N1):
  *      help            -> list all commands (built-in + these)
  *      kernel uptime   -> Zephyr built-in: run time in ms
  *      sys             -> version / uptime / core clock
  *      font            -> SD card GBK font file status (FONT_MASK_*)
  *      lvmem           -> LVGL heap statistics
  *      echo hello 42   -> argument parsing demo
  *
  *  Built-in commands provided by the shell subsystem (no code needed):
  *      help, clear, history, resize, kernel (uptime/threads/stacks/...),
  *      device, date, log ...
  ******************************************************************************
  */
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include <string.h>

#include "drv_oled_text.h"   /* lcd_driver_font_status() -> FONT_MASK_*        */
#include "lv_port_font.h"    /* project LVGL fonts                             */
#include <lvgl.h>            /* LVGL version + lv_mem_monitor                  */

/* ---------------------------------------------------------------------------
 * sys — system information: Zephyr/LVGL version, uptime, core clock.
 * -------------------------------------------------------------------------*/
static int cmd_sys(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Zephyr %s  LVGL %d.%d.%d",
                KERNEL_VERSION_STRING,
                LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    shell_print(sh, "uptime    : %u ms", (unsigned)k_uptime_get_32());
    shell_print(sh, "core clock: %u Hz",
                (unsigned)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
    shell_print(sh, "stack     : main=%u",
                (unsigned)CONFIG_MAIN_STACK_SIZE);
    return 0;
}

/* ---------------------------------------------------------------------------
 * font — SD card GBK font file status (UNIGBK.BIN + GBK12/16/24/32.FON).
 * -------------------------------------------------------------------------*/
static int cmd_font(const struct shell *sh, size_t argc, char **argv)
{
    uint32_t mask = lcd_driver_font_status();

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "font mask = 0x%02X", (unsigned)mask);
    shell_print(sh, "  UNIGBK.BIN : %s", (mask & FONT_MASK_UNIGBK) ? "loaded" : "missing");
    shell_print(sh, "  GBK12.FON  : %s", (mask & FONT_MASK_GBK12)  ? "loaded" : "missing");
    shell_print(sh, "  GBK16.FON  : %s", (mask & FONT_MASK_GBK16)  ? "loaded" : "missing");
    shell_print(sh, "  GBK24.FON  : %s", (mask & FONT_MASK_GBK24)  ? "loaded" : "missing");
    shell_print(sh, "  GBK32.FON  : %s", (mask & FONT_MASK_GBK32)  ? "loaded" : "missing");
    return 0;
}

/* ---------------------------------------------------------------------------
 * lvmem — LVGL heap usage (works when LV_MEM_CUSTOM=0, i.e. the LVGL built-in
 *         static heap; this project sizes it via CONFIG_LV_MEM_SIZE_KILOBYTES).
 * -------------------------------------------------------------------------*/
static int cmd_lvmem(const struct shell *sh, size_t argc, char **argv)
{
    lv_mem_monitor_t mon;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    lv_mem_monitor(&mon);
    shell_print(sh, "LVGL heap  : total=%u B", (unsigned)mon.total_size);
    shell_print(sh, "            free=%u B  biggest=%u B",
                (unsigned)mon.free_size, (unsigned)mon.free_biggest_size);
    shell_print(sh, "            used=%u%%  frag=%u%%",
                (unsigned)mon.used_pct, (unsigned)mon.frag_pct);
    return 0;
}

/* ---------------------------------------------------------------------------
 * echo — argument parsing demo (portable template for argc/argv handling).
 * -------------------------------------------------------------------------*/
static int cmd_echo(const struct shell *sh, size_t argc, char **argv)
{
    size_t i;

    for (i = 1; i < argc; i++)
    {
        shell_fprintf(sh, SHELL_NORMAL, "%s%s", (i > 1) ? " " : "", argv[i]);
    }
    shell_print(sh, "");
    return 0;
}

/* ---------------------------------------------------------------------------
 * Registration — copy these four SHELL_CMD_REGISTER lines into any Zephyr app
 * together with this file and the commands appear on the serial console.
 * -------------------------------------------------------------------------*/
SHELL_CMD_REGISTER(sys,    NULL, "System info: version / uptime / core clock", cmd_sys);
SHELL_CMD_REGISTER(font,   NULL, "Show SD card GBK font file status",          cmd_font);
SHELL_CMD_REGISTER(lvmem,  NULL, "Show LVGL heap usage",                       cmd_lvmem);
SHELL_CMD_REGISTER(echo,   NULL, "Echo arguments (arg parsing demo)",          cmd_echo);
