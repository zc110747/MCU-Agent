/**
  * @file    page_camera.c
  * @brief   Camera preview page: a live 192x192 centred RGB565 viewfinder.
  *
  *  This is a full_screen page: while it is on top the main loop stops
  *  servicing LVGL and the camera tick owns the panel directly through
  *  LCD_CopyBuffer().  The capture buffers come from the shared sram_pool
  *  (RAM_D2) - the same pool the NES emulator uses - so opening the camera
  *  takes ~221 kB of RAM and closing it hands every byte back.
  *
  *  Tear-free preview
  *  -----------------
  *  The DCMI DMA runs a hardware double buffer into s_frame[0]/[1].  Each
  *  main-loop tick arms a snapshot (drv_camera_request_snapshot); the DMA
  *  ISR, on the next completed frame, invalidates the cache for the buffer it
  *  just wrote and memcpy's it into the third buffer s_frame[2].  The tick
  *  only blits when that snapshot is ready, so a half-written frame can never
  *  reach the panel - and because the tick is non-blocking the rest of the
  *  system (serial console, other keys) keeps breathing between frames.
  *
 *  The 192x192 image is centred on the 240x240 panel, leaving a 24px black
 *  margin on every side (no OSD text).  Pressing BACK / MENU / SELECT / B
 *  exits the page.
 */
#include "app_page.h"
#include "menu_icons.h"
#include "drv_spi_oled.h"
#include "drv_camera.h"
#include "bsp_key.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "bsp_log.h"

/* ------------------------------------------------------------------- state */
static uint8_t s_open = 0U;     /* page entered (on_enter called)        */
static uint8_t s_cam_running = 0U; /* DMA capture actually started       */

/* ------------------------------------------------------------------- hooks */
static void cam_enter(lv_obj_t *root)
{
    (void)root;                 /* full_screen: nothing to build in LVGL */

    s_open = 1U;
    s_cam_running = 0U;

    /* OV5640 init + crop + buffer allocation all happen here.  On a board
     * without a sensor this fails and the page stays blank until BACK. */
    if (drv_camera_open() != RT_OK)
    {
        PRINT_LOG("[CAM ] open failed - no sensor / i2c error?\r\n");
        return;
    }

    LCD_SetBackColor(LCD_BLACK);
    LCD_Clear();

    if (drv_camera_start() != RT_OK)
    {
        PRINT_LOG("[CAM ] start failed\r\n");
        drv_camera_close();
        return;
    }

    s_cam_running = 1U;
    PRINT_LOG("[CAM ] live preview started - 按 BACK/MENU/SELECT/B 退出\r\n");
}

static void cam_exit(void)
{
    drv_camera_close();         /* stop DMA + return the 3 buffers to pool */
    s_cam_running = 0U;
    s_open = 0U;
    bsp_key_release_all();
    PRINT_LOG("[CAM ] page closed, buffers released\r\n");
}

static int cam_key(key_id_t id, key_edge_t edge)
{
    /* Full-screen page: swallow everything except the explicit exits so the
     * menu does not try to act on game/joystick keys. */
    if ((edge == KEY_EV_DOWN) &&
        ((id == KEY_BACK) || (id == KEY_MENU) || (id == KEY_SELECT) ||
         (id == KEY_B)))
    {
        app_menu_back();
        return 1;
    }
    return 1;
}

static void cam_tick(void)
{
    if (!s_cam_running)
    {
        return;
    }

    /* Arm the next completed frame, then blit only if the ISR already
     * copied one into the snapshot buffer (non-blocking, tear-free). */
    drv_camera_request_snapshot();

    if (drv_camera_snapshot_ready())
    {
        LCD_CopyBuffer((uint16_t)CAM_DISP_OFFSET, (uint16_t)CAM_DISP_OFFSET,
                       (uint16_t)CAM_WIDTH, (uint16_t)CAM_HEIGHT,
                       drv_camera_snapshot_ptr());
        drv_camera_snapshot_done();
    }
}

static int cam_wants_display(void)
{
    return 1;                   /* always full_screen while open */
}

static const app_page_t s_page_camera =
{
    .title         = "相机",
    .hint          = "实时 192x192 预览",
    .cmd           = "camera",
    .full_screen   = 1U,
    .icon          = &icon_camera,
    .on_enter      = cam_enter,
    .on_exit       = cam_exit,
    .on_key        = cam_key,
    .on_tick       = cam_tick,
    .wants_display = cam_wants_display
};

const app_page_t *page_camera_get(void)
{
    return &s_page_camera;
}

/* ------------------------------------------------------------------- console */
int page_camera_is_open(void)
{
    return (s_open != 0U) ? 1 : 0;
}

int page_camera_is_running(void)
{
    return (s_cam_running != 0U) ? 1 : 0;
}

void page_camera_stop(void)
{
    if (s_open)
    {
        app_menu_back();
    }
}

void page_camera_info(char *out, int out_size)
{
    if (!s_open)
    {
        (void)snprintf(out, (size_t)out_size, "closed");
        return;
    }
    if (!s_cam_running)
    {
        (void)snprintf(out, (size_t)out_size, "open, init failed");
        return;
    }
    (void)snprintf(out, (size_t)out_size,
                   "live %dfps, frames %lu, overruns %lu",
                   (int)g_cam_fps,
                   (unsigned long)g_cam_frames,
                   (unsigned long)g_cam_overruns);
}
