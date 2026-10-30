/**
 ******************************************************************************
 * @file    app_ui.c
 * @brief   Two-screen UI: the player page and the USB transfer page.
 *
 *  Player page (800x480 landscape):
 *
 *      +---------------------------------------------------+
 *      |  MUSIC PLAYER                                     |
 *      |                                                   |
 *      |  <track file name>                                |
 *      |                                                   |
 *      |  [=========================-------------]          |
 *      |  01:23                                   03:45     |
 *      |                                                   |
 *      |  PLAYING                      44.1 kHz / 16 bit   |
 *      |                                                   |
 *      |  KEY_UP: play/pause  KEY0: next  KEY1: vol  KEY2: prev   [VOL 3/5]
 *      +---------------------------------------------------+
 *
 *  Transfer page (shown while the PC owns the card):
 *
 *      +---------------------------------------------------+
 *      |  USB STORAGE                                      |
 *      |  DATA TRANSFER                                    |
 *      |  The SD card is shared with your PC.              |
 *      |  TOTAL 7450 MB   USED 1234 MB   FREE 6216 MB      |
 *      |  [===========-----------------------------]       |
 *      |  USB CONNECTED                                    |
 *      |  COPY FILES FROM YOUR PC. UNPLUG THE CABLE TO RETURN.
 *      +---------------------------------------------------+
 *
 *  Only the widgets that actually change are refreshed, and only when their
 *  value changed, so the FMC 8080 panel (polled, no LTDC) is not asked to push
 *  pixels it has already shown.
 *
 *  The track name is cached as a string, not as a playlist index: the first
 *  track is index 0, which is exactly the value a zero-initialised cache would
 *  hold, so an index-only comparison silently skips the very first redraw and
 *  the player boots showing an empty name label.
 *
 *  Note on file names: LVGL's built-in Montserrat faces cover Latin-1 only, so
 *  non-ASCII bytes in a track name are replaced with '?' here.  Feeding them
 *  through unchanged would render as blank boxes.
 *
 *  Note on the transfer page: its text is deliberately ASCII only.  CJK glyphs
 *  are fetched from GBKxx.FON on the SD card at draw time, and during a USB
 *  session that card belongs to the PC -- bsp_lcd_text.c refuses the read (the
 *  glyph would render as a gap), so a Chinese label there would come out blank.
 ******************************************************************************
 */
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lvgl.h"

#include "app_ui.h"
#include "audio_player.h"
#include "bsp_key.h"
#include "bsp_lcd.h"
#include "bsp_lcd_text.h"
#include "bsp_wm8978.h"
#include "fs_diskio.h"
#include "lv_font_gbk.h"
#include "lv_port_disp.h"
#include "log.h"
#include "sd_card.h"
#include "usb_msc.h"

#define UI_TASK_STACK 1024U
#define UI_TASK_PRIO (tskIDLE_PRIORITY + 2)
#define UI_PERIOD_MS 10U

/* Palette: matching the project-wide minimal dark style (no accent colours). */
#define COLOR_BG 0x0E0F11U
#define COLOR_PANEL 0x1A1C1FU
#define COLOR_TEXT 0xE8E8E8U
#define COLOR_TEXT_DIM 0x8A8F96U
#define COLOR_BAR_BG 0x2A2D31U
#define COLOR_BAR_VALUE 0xBFC4CAU

#define BAR_RANGE 1000

typedef struct
{
    lv_obj_t *title_label;
    lv_obj_t *name_label;
    lv_obj_t *time_label;
    lv_obj_t *state_label;
    lv_obj_t *format_label;
    lv_obj_t *count_label;
    lv_obj_t *volume_label;
    lv_obj_t *bar;
    lv_obj_t *hint_label;

    char     shown_name[96];
    uint32_t shown_seconds;
    uint32_t shown_total;
    uint16_t shown_files;
    uint16_t shown_tracks;
    int16_t  shown_index;
    uint8_t  shown_state;
    uint8_t  shown_volume;
} UI_WIDGETS_T;

static UI_WIDGETS_T s_ui = {0};

/* Transfer page.  Everything here is refreshed exactly once, when the page is
 * about to be shown -- the requirement is a snapshot, not a live gauge, and the
 * FAT is unreadable while the host owns it anyway. */
typedef struct
{
    lv_obj_t *capacity_label;
    lv_obj_t *free_label;
    lv_obj_t *bar;
} UI_USB_T;

static UI_USB_T s_usb = {0};

static lv_obj_t *s_screen_player = NULL;
static lv_obj_t *s_screen_usb = NULL;

/* Last USB state painted, so the screens are swapped on transition only. */
static USB_MSC_STATE_T s_shown_usb_state = USB_MSC_STATE_IDLE;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */
static void format_mmss(char *out, uint32_t out_size, uint32_t seconds)
{
    (void)snprintf(out, out_size, "%02lu:%02lu",
                   (unsigned long)(seconds / 60U), (unsigned long)(seconds % 60U));
}

/* -------------------------------------------------------------------------- */
/* Screen construction                                                        */
/* -------------------------------------------------------------------------- */
static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, "");
    return label;
}

static void build_player_screen(lv_obj_t *screen)
{
    lv_obj_t *panel;

    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    s_ui.title_label = make_label(screen, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(s_ui.title_label, "MUSIC PLAYER");
    lv_obj_align(s_ui.title_label, LV_ALIGN_TOP_LEFT, 40, 28);

    /* Card holding the track information */
    panel = lv_obj_create(screen);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LCD_WIDTH - 80, 250);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Track name */
    s_ui.name_label = make_label(panel, &lv_font_gbk_24, COLOR_TEXT);
    lv_label_set_long_mode(s_ui.name_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_ui.name_label, LCD_WIDTH - 160);
    lv_obj_align(s_ui.name_label, LV_ALIGN_TOP_LEFT, 40, 36);
    lv_label_set_text(s_ui.name_label, "--");

    /* Progress bar */
    s_ui.bar = lv_bar_create(panel);
    lv_obj_set_size(s_ui.bar, LCD_WIDTH - 160, 10);
    lv_obj_align(s_ui.bar, LV_ALIGN_TOP_MID, 0, 116);
    lv_bar_set_range(s_ui.bar, 0, BAR_RANGE);
    lv_bar_set_value(s_ui.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_ui.bar, lv_color_hex(COLOR_BAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.bar, lv_color_hex(COLOR_BAR_VALUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.bar, 5, LV_PART_INDICATOR);

    /* Elapsed / total */
    s_ui.time_label = make_label(panel, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(s_ui.time_label, "00:00 / 00:00");
    lv_obj_align(s_ui.time_label, LV_ALIGN_TOP_LEFT, 40, 140);

    /* Playback state (left) and stream format (right) */
    s_ui.state_label = make_label(panel, &lv_font_gbk_16, COLOR_TEXT);
    lv_label_set_text(s_ui.state_label, "IDLE");
    lv_obj_align(s_ui.state_label, LV_ALIGN_TOP_LEFT, 40, 190);

    s_ui.format_label = make_label(panel, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(s_ui.format_label, "");
    lv_obj_align(s_ui.format_label, LV_ALIGN_TOP_RIGHT, -40, 190);

    /* Folder inventory: every file vs. the playable ones, plus the position in
     * the playlist.  A file that is present but unplayable is then visible
     * instead of silently ignored. */
    s_ui.count_label = make_label(panel, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(s_ui.count_label, "");
    lv_obj_align(s_ui.count_label, LV_ALIGN_TOP_LEFT, 40, 220);

    /* Key hints */
    s_ui.hint_label = make_label(screen, &lv_font_gbk_12, COLOR_TEXT_DIM);
    lv_label_set_text(s_ui.hint_label,
                      "KEY_UP: play/pause    KEY0: next    KEY1: volume    KEY2: prev");
    lv_obj_align(s_ui.hint_label, LV_ALIGN_BOTTOM_MID, 0, -30);

    /* Volume read-out, right aligned on the same row as the hint */
    s_ui.volume_label = make_label(screen, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(s_ui.volume_label, "");
    lv_obj_align(s_ui.volume_label, LV_ALIGN_BOTTOM_LEFT, 40, -28);
}

static void build_usb_screen(lv_obj_t *screen)
{
    lv_obj_t *panel;

    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Header */
    lv_obj_t *title = make_label(screen, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(title, "USB STORAGE");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 40, 28);

    panel = lv_obj_create(screen);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LCD_WIDTH - 80, 250);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    /* Headline */
    lv_obj_t *headline = make_label(panel, &lv_font_gbk_24, COLOR_TEXT);
    lv_label_set_text(headline, "DATA TRANSFER");
    lv_obj_align(headline, LV_ALIGN_TOP_LEFT, 40, 30);

    lv_obj_t *sub = make_label(panel, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(sub, "The SD card is shared with your PC.");
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 40, 80);

    /* Capacity snapshot: total and used, frozen for the whole session */
    s_usb.capacity_label = make_label(panel, &lv_font_gbk_16, COLOR_TEXT);
    lv_label_set_text(s_usb.capacity_label, "TOTAL -- MB   USED -- MB");
    lv_obj_align(s_usb.capacity_label, LV_ALIGN_TOP_LEFT, 40, 130);

    s_usb.free_label = make_label(panel, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(s_usb.free_label, "");
    lv_obj_align(s_usb.free_label, LV_ALIGN_TOP_LEFT, 40, 160);

    /* Used / total ratio, same visual language as the progress bar */
    s_usb.bar = lv_bar_create(panel);
    lv_obj_set_size(s_usb.bar, LCD_WIDTH - 160, 12);
    lv_obj_align(s_usb.bar, LV_ALIGN_TOP_LEFT, 40, 200);
    lv_bar_set_range(s_usb.bar, 0, BAR_RANGE);
    lv_bar_set_value(s_usb.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_usb.bar, lv_color_hex(COLOR_BAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_usb.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_usb.bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_usb.bar, lv_color_hex(COLOR_BAR_VALUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_usb.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_usb.bar, 6, LV_PART_INDICATOR);

    /* Connection state */
    lv_obj_t *state = make_label(screen, &lv_font_gbk_16, COLOR_TEXT_DIM);
    lv_label_set_text(state, "USB CONNECTED");
    lv_obj_align(state, LV_ALIGN_BOTTOM_LEFT, 40, -28);

    /* Instruction line */
    lv_obj_t *hint = make_label(screen, &lv_font_gbk_12, COLOR_TEXT_DIM);
    lv_label_set_text(hint,
                      "COPY FILES FROM YOUR PC.  UNPLUG THE USB CABLE TO RETURN TO THE PLAYER.");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

/**
 * @brief  Fill the transfer page with the capacity snapshot taken when the
 *         session started.  Called once, right before the page is shown.
 */
static void update_usb_screen(void)
{
    char     text[96];
    uint32_t total_mb = 0U;
    uint32_t used_mb = 0U;
    uint32_t free_mb;
    int32_t  ratio = 0;

    usb_msc_capacity_mb(&total_mb, &used_mb);

    if (total_mb == 0U)
    {
        /* The FSInfo did not carry a free-cluster count and FatFs refused to
         * walk the whole FAT (see sd_card_usage_mb).  Say so instead of
         * printing a fake 0 MB. */
        lv_label_set_text(s_usb.capacity_label, "TOTAL -- MB   USED -- MB");
        lv_label_set_text(s_usb.free_label, "");
        lv_bar_set_value(s_usb.bar, 0, LV_ANIM_OFF);
        return;
    }

    free_mb = (total_mb > used_mb) ? (total_mb - used_mb) : 0U;

    (void)snprintf(text, sizeof(text), "TOTAL %lu MB   USED %lu MB",
                   (unsigned long)total_mb, (unsigned long)used_mb);
    lv_label_set_text(s_usb.capacity_label, text);

    (void)snprintf(text, sizeof(text), "FREE %lu MB", (unsigned long)free_mb);
    lv_label_set_text(s_usb.free_label, text);

    if (total_mb != 0U)
    {
        ratio = (int32_t)(((uint32_t)used_mb * BAR_RANGE) / total_mb);
        if (ratio > BAR_RANGE)
        {
            ratio = BAR_RANGE;
        }
    }
    lv_bar_set_value(s_usb.bar, ratio, LV_ANIM_OFF);
}
/* -------------------------------------------------------------------------- */
/* Refresh                                                                    */
/* -------------------------------------------------------------------------- */
static void refresh_from_status(void)
{
    const AUDIO_STATUS_T *status = audio_player_status();
    char                  name[96];
    char                  time_text[40];
    char                  format_text[32];
    char                  volume_text[16];
    int32_t               bar_value = 0;
    uint8_t               state     = (uint8_t)status->state;
    uint8_t               volume    = bsp_wm8978_volume_level();

    /* Compare the text, not the playlist index: the first track is index 0, so
     * an index comparison against the zero-initialised cache would never fire
     * on the very first track and the name label would stay empty. */
    if ((status->name[0] != '\0') && (strcmp(status->name, s_ui.shown_name) != 0))
    {
        (void)snprintf(name, sizeof(name), "%s", status->name);
        lv_label_set_text(s_ui.name_label, name);
        (void)snprintf(s_ui.shown_name, sizeof(s_ui.shown_name), "%s", status->name);
    }

    if (volume != s_ui.shown_volume)
    {
        (void)snprintf(volume_text, sizeof(volume_text), "VOL %u/%u",
                       (unsigned)volume, (unsigned)WM8978_VOLUME_LEVELS);
        lv_label_set_text(s_ui.volume_label, volume_text);
        s_ui.shown_volume = volume;
    }

    if (status->total_seconds != 0U)
    {
        bar_value = (int32_t)(((uint32_t)status->current_seconds * BAR_RANGE) /
                              status->total_seconds);
        if (bar_value > BAR_RANGE)
        {
            bar_value = BAR_RANGE;
        }
    }

    if ((status->current_seconds != s_ui.shown_seconds) ||
        (status->total_seconds != s_ui.shown_total))
    {
        char elapsed[12];
        char total[12];

        format_mmss(elapsed, sizeof(elapsed), status->current_seconds);
        format_mmss(total, sizeof(total), status->total_seconds);
        (void)snprintf(time_text, sizeof(time_text), "%s / %s", elapsed, total);
        lv_label_set_text(s_ui.time_label, time_text);
        lv_bar_set_value(s_ui.bar, bar_value, LV_ANIM_OFF);

        s_ui.shown_seconds = status->current_seconds;
        s_ui.shown_total   = status->total_seconds;
    }

    if (state != s_ui.shown_state)
    {
        switch (state)
        {
        case AUDIO_STATE_PLAYING:
            lv_label_set_text(s_ui.state_label, "PLAYING");
            break;
        case AUDIO_STATE_PAUSED:
            lv_label_set_text(s_ui.state_label, "PAUSED");
            break;
        case AUDIO_STATE_ERROR:
            lv_label_set_text(s_ui.state_label, "NO CARD / NO MUSIC");
            break;
        default:
            lv_label_set_text(s_ui.state_label, "IDLE");
            break;
        }
        s_ui.shown_state = state;
    }

    if (status->sample_rate != 0U)
    {
        (void)snprintf(format_text, sizeof(format_text), "%s %u Hz / %u bit",
                       (status->is_mp3 != 0U) ? "MP3" : "WAV",
                       (unsigned)status->sample_rate,
                       (unsigned)status->bits_per_sample);
        lv_label_set_text(s_ui.format_label, format_text);
    }

    if ((status->dir_files != s_ui.shown_files) ||
        (status->track_count != s_ui.shown_tracks) ||
        (status->track != s_ui.shown_index))
    {
        char count_text[48];

        if (status->track >= 0)
        {
            (void)snprintf(count_text, sizeof(count_text),
                           "FILES %u   MUSIC %u   NOW %d/%u",
                           (unsigned)status->dir_files,
                           (unsigned)status->track_count,
                           (int)status->track + 1,
                           (unsigned)status->track_count);
        }
        else
        {
            (void)snprintf(count_text, sizeof(count_text),
                           "FILES %u   MUSIC %u   NOW -/%u",
                           (unsigned)status->dir_files,
                           (unsigned)status->track_count,
                           (unsigned)status->track_count);
        }
        lv_label_set_text(s_ui.count_label, count_text);

        s_ui.shown_files  = status->dir_files;
        s_ui.shown_tracks = status->track_count;
        s_ui.shown_index  = status->track;
    }
}

/* -------------------------------------------------------------------------- */
/* Task                                                                       */
/* -------------------------------------------------------------------------- */
static void ui_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        USB_MSC_STATE_T usb_state;

        lv_timer_handler();

        /* The transfer page follows the USB hand-over state: it appears as soon
         * as the host has enumerated us, and goes away when the cable is
         * pulled.  Both edges are the only place the pages are swapped. */
        usb_state = usb_msc_state();
        if (usb_state != s_shown_usb_state)
        {
            if (usb_state == USB_MSC_STATE_ACTIVE)
            {
                update_usb_screen();
                lv_scr_load(s_screen_usb);
            }
            else
            {
                /* The host may have replaced or removed anything on the card,
                 * including the GBK glyph files: reopen them and drop the
                 * cached glyph bitmaps before the player page is drawn. */
                if (sd_card_is_ready() != 0)
                {
                    (void)lcd_driver_font_init(FS_VOL_SD);
                }
                lv_font_gbk_reset_cache();
                lv_scr_load(s_screen_player);
            }
            s_shown_usb_state = usb_state;
        }

        if (usb_state == USB_MSC_STATE_ACTIVE)
        {
            /* The card belongs to the PC: no key handling (the player is
             * parked) and no status refresh, which would only repaint hidden
             * widgets.  lv_timer_handler above keeps the page alive. */
            vTaskDelay(pdMS_TO_TICKS(UI_PERIOD_MS));
            continue;
        }

        switch (bsp_key_scan())
        {
        case BSP_KEY_EVENT_PLAY_PAUSE:
            audio_player_toggle_pause();
            break;
        case BSP_KEY_EVENT_NEXT:
            audio_player_next();
            break;
        case BSP_KEY_EVENT_PREV:
            audio_player_prev();
            break;
        case BSP_KEY_EVENT_VOLUME: {
            uint8_t level = bsp_wm8978_volume_step();

            PRINT_LOG("[KEY ] KEY1 -> volume %u/%u\r\n",
                      (unsigned)level, (unsigned)WM8978_VOLUME_LEVELS);
            break;
        }
        default:
            break;
        }

        refresh_from_status();
        vTaskDelay(pdMS_TO_TICKS(UI_PERIOD_MS));
    }
}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */
void app_ui_init(void)
{
    lv_init();
    lv_port_disp_init();

    s_screen_player = lv_obj_create(NULL);
    build_player_screen(s_screen_player);

    s_screen_usb = lv_obj_create(NULL);
    build_usb_screen(s_screen_usb);

    lv_scr_load(s_screen_player);

    /* Force the first refresh to paint the track name / counters: the cache
     * must start at "nothing shown yet", not at a value that can equal a real
     * track index. */
    s_ui.shown_index    = -1;
    s_shown_usb_state   = usb_msc_state();

    PRINT_LOG("[UI  ] LVGL %d.%d.%d screen up (%ux%u)\r\n",
              LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
              (unsigned)LCD_WIDTH, (unsigned)LCD_HEIGHT);
}

void app_ui_start(void)
{
    (void)xTaskCreate(ui_task, "ui", UI_TASK_STACK, NULL, UI_TASK_PRIO, NULL);
}
