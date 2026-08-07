/**
 * @file    app_face.c
 * @brief   OV5640 -> CMSIS-NN face detector -> 240x240 ST7789 panel.
 *
 * Triple buffering (this is what keeps the picture from tearing)
 * ---------------------------------------------------------------------------
 *   s_frame[0] / s_frame[1]   drv_dcmi.c, .dma_buffer @0x24000000, NON-cached
 *                             the DCMI DMA ping-pongs between these two and
 *                             the CPU never writes them.
 *   s_disp                    this file, .axi_ram @0x24040000, cacheable
 *                             CPU-only third buffer.  The moment a frame
 *                             completes we snapshot it here - at that instant
 *                             the DMA has already switched to the other
 *                             capture buffer - and everything afterwards
 *                             (inference, box overlay, the ~10 ms SPI blit)
 *                             works on this private copy.  The camera can
 *                             therefore never write into the buffer that is
 *                             being sent to the panel, so a refresh always
 *                             shows one complete frame.
 *
 * Frame flow
 * ---------------------------------------------------------------------------
 *   OV5640 320x240 RGB565
 *     -> DCMI centre crop 192x192            (drv_dcmi.c, hardware crop)
 *     -> snapshot into s_disp                (memcpy, ~37 k pixels)
 *     -> 2x2 box filter + luma -> 96x96 int8  (fd_preprocess_rgb565)
 *     -> CenterNet-style detector             (fd_run, CMSIS-NN int8)
 *     -> boxes drawn into s_disp, x2 because 96 -> 192
 *     -> single LCD_CopyBuffer blit at (24,0)
 *
 * Screen layout (240x240)
 * ---------------------------------------------------------------------------
 *   y   0..191   live 192x192 preview with the detection boxes, centred
 *   y 192..215   "FACE:n  P:xxx%"   face count + best confidence  (24 px font)
 *   y 218..233   "nn fps / latency / overruns"                    (16 px font)
 *
 * Set -DAPP_ENABLE_DETECT=0 to build the preview-only variant, which is the
 * bring-up step for the camera and the panel on their own.
 */
#include <stdio.h>
#include <string.h>

#include "app_face.h"
#include "drv_dcmi.h"
#include "drv_spi_oled.h"
#include "fd_infer.h"
#include "logger.h"

/* ------------------------------------------------------------------ build */
#ifndef APP_ENABLE_DETECT
#define APP_ENABLE_DETECT       1
#endif

/* ----------------------------------------------------------------- layout */
#define VIEW_W                  CAPTURE_WIDTH               /* 192           */
#define VIEW_H                  CAPTURE_HEIGHT              /* 192           */
#define VIEW_X                  ((LCD_WIDTH - VIEW_W) / 2)  /* 24, centred   */
#define VIEW_Y                  0

#define INFO_X                  4
#define INFO_Y1                 (VIEW_Y + VIEW_H)           /* 192, font 24  */
#define INFO_Y2                 (INFO_Y1 + 26)              /* 218, font 16  */

/* 96x96 detector coordinates -> 192x192 preview coordinates */
#define BOX_SCALE               (VIEW_W / FD_INPUT_W)       /* 2             */
#define BOX_THICK               2

/* RGB565, the storage format LCD_CopyBuffer expects */
#define RGB565_GREEN            0x07E0u
#define RGB565_RED              0xF800u
#define RGB565_YELLOW           0xFFE0u

/* Detection debounce (asymmetric hysteresis): appear fast, disappear slowly.
 *
 *   - The box is drawn the instant a face is detected (zero added latency).
 *   - It is only removed once BOTH of these are true:
 *       (a) the detector has come back empty for MISS_FRAMES_TO_CLEAR
 *           consecutive frames  ("multiple checks"), so a single dropped
 *           frame - someone blinking or briefly turning their head - can
 *           never blink the box off, and a lone false-positive frame that
 *           slipped in cannot flash a box on-screen for one frame either; and
 *       (b) FACE_HOLD_MS have elapsed since the last real hit.
 *
 * This is what keeps the overlay rock-steady and suppresses both flicker and
 * momentary mis-detections. */
#define FACE_HOLD_MS            1000u   /* linger up to 1 s after the last hit */
#define MISS_FRAMES_TO_CLEAR    5u      /* consecutive empty frames to confirm */

#define TEXT_CACHE_LEN          31

/* ------------------------------------------------------------------ state */
/* Third buffer: display only.  CPU writes it, DMA never does. */
AXI_RAM static uint16_t s_disp[CAPTURE_PIXELS];

static uint8_t    s_fd_ready;
static uint32_t   s_overruns;

static uint32_t   s_stat_tick;
static uint32_t   s_loop_count;
static uint8_t    s_loop_fps;

static fd_result_t s_res;          /* last successful detection             */
static fd_result_t s_shown;        /* what is currently drawn (with hold)   */
static uint32_t    s_last_face_tick;
static uint16_t    s_miss_count;   /* consecutive frames with no detection  */

static char       s_line1[TEXT_CACHE_LEN + 1];
static char       s_line2[TEXT_CACHE_LEN + 1];

static void show_text(uint16_t x, uint16_t y, char *cache, const char *text);
static void draw_box(uint16_t *buf, int x, int y, int w, int h,
                     uint16_t colour, int thick);
static void overlay_boxes(const fd_result_t *res);
static void fatal_screen(const char *msg);

/* ------------------------------------------------------------------- init */
GlobalType_t app_face_init(void)
{
    PRINT_LOG(LOG_INFO, HAL_GetTick(),
              "=== STM32H743 face detect (%s) ===",
              APP_ENABLE_DETECT ? "camera + NN" : "camera preview only");

    /* 1. Panel first, so later failures have somewhere to be reported. */
    if (driver_spi_oled_init() != RT_OK)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "oled init failed");
        return RT_FAIL;
    }
    LCD_SetBackColor(LCD_BLACK);
    LCD_SetColor(LCD_WHITE);
    LCD_Clear();
    LCD_SetAsciiFont(&ASCII_Font24);
    LCD_DisplayText(VIEW_X, 100, "booting...");

    /* 2. Neural network: cheap, and a failure here is worth showing early. */
#if APP_ENABLE_DETECT
    fd_init();
    s_fd_ready = 1;
#else
    s_fd_ready = 0;
#endif

    /* 3. Camera. */
    if (drv_dcmi_init() != RT_OK)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "camera init failed");
        fatal_screen("CAM INIT FAIL");
        return RT_FAIL;
    }

    memset(s_disp, 0, sizeof(s_disp));
    memset(&s_res, 0, sizeof(s_res));
    memset(&s_shown, 0, sizeof(s_shown));
    LCD_Clear();

    if (drv_dcmi_start() != RT_OK)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "camera start failed");
        fatal_screen("CAM START FAIL");
        return RT_FAIL;
    }

    s_overruns       = g_dcmi_overruns;
    s_stat_tick      = HAL_GetTick();
    s_loop_count     = 0u;
    s_last_face_tick = 0u;
    s_miss_count     = 0u;

    PRINT_LOG(LOG_INFO, HAL_GetTick(),
              "pipeline running: %dx%d capture -> %dx%d input -> %dx%d panel",
              CAPTURE_WIDTH, CAPTURE_HEIGHT, FD_INPUT_W, FD_INPUT_H,
              LCD_WIDTH, LCD_HEIGHT);
    return RT_OK;
}

/* ------------------------------------------------------------------- loop */
void app_face_loop(void)
{
    uint16_t *frame = NULL;
    uint32_t  now   = HAL_GetTick();
    char      buf[40];
    uint8_t   faces = 0u;
    uint8_t   peak  = 0u;

    /* An overrun leaves the DCMI stalled - re-arm and try again next round. */
    if (g_dcmi_overruns != s_overruns)
    {
        s_overruns = g_dcmi_overruns;
        PRINT_LOG(LOG_WARN, now, "dcmi overrun #%lu, restarting",
                  (unsigned long)s_overruns);
        drv_dcmi_recover();
        return;
    }

    /* Wait for a frame the DMA has finished with. */
    if (drv_dcmi_get_frame(&frame) != RT_OK)
    {
        return;
    }

    /* Snapshot into the private display buffer.  From here on the camera is
     * filling the *other* capture buffer, so nothing we touch can change. */
    memcpy(s_disp, frame, CAPTURE_BYTES);

    /* ------------------------------------------------------------ detect */
#if APP_ENABLE_DETECT
    if (s_fd_ready)
    {
        fd_preprocess_rgb565(s_disp, CAPTURE_WIDTH, fd_input_buffer());

        if (fd_run(&s_res) == RT_OK)
        {
            if (s_res.count > 0u)
            {
                /* Hit: show it immediately and reset the miss debounce. */
                s_shown          = s_res;
                s_last_face_tick = now;
                s_miss_count     = 0u;
                LED_ON();
            }
            else
            {
                /* Miss: count consecutive empties.  Only drop the box once it
                 * has been missed enough times AND the 1 s hold has expired,
                 * so brief drop-outs (and one-frame false hits) stay invisible. */
                if (s_miss_count < 0xFFFFu)
                {
                    s_miss_count++;
                }
                if (s_shown.count > 0u &&
                    s_miss_count >= (uint16_t)MISS_FRAMES_TO_CLEAR &&
                    (now - s_last_face_tick) >= (uint32_t)FACE_HOLD_MS)
                {
                    s_shown.count = 0u;
                    LED_OFF();
                }
                /* Status bar always tracks the live confidence. */
                s_shown.peak = s_res.peak;
            }
        }
    }
#endif
    faces = s_shown.count;
    peak  = s_shown.peak;

    /* --------------------------------------------------------- compose */
    overlay_boxes(&s_shown);
    LCD_CopyBuffer(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, s_disp);

    /* ------------------------------------------------------------ stats */
    s_loop_count++;
    if ((now - s_stat_tick) >= 1000u)
    {
        s_stat_tick  = now;
        s_loop_fps   = (uint8_t)s_loop_count;
        s_loop_count = 0u;

        PRINT_LOG(LOG_INFO, now,
                  "cam %2u fps | pipe %2u fps | nn %5lu us | faces %u | peak %u%%"
                  " | ovr %lu",
                  g_dcmi_fps, s_loop_fps, (unsigned long)s_res.infer_us,
                  faces, (unsigned)((peak * 100u + 127u) / 255u),
                  (unsigned long)s_overruns);
    }

    /* ------------------------------------------------------------- text */
    snprintf(buf, sizeof(buf), "FACE:%u  P:%3u%%",
             (unsigned)(faces > 9u ? 9u : faces),
             (unsigned)((peak * 100u + 127u) / 255u));
    LCD_SetAsciiFont(&ASCII_Font24);
    LCD_SetColor(faces ? LCD_GREEN : LCD_WHITE);
    show_text(INFO_X, INFO_Y1, s_line1, buf);

    snprintf(buf, sizeof(buf), "%2ufps nn%3lums ov%lu",
             (unsigned)s_loop_fps,
             (unsigned long)((s_res.infer_us + 500u) / 1000u),
             (unsigned long)(s_overruns > 99u ? 99u : s_overruns));
    LCD_SetAsciiFont(&ASCII_Font16);
    LCD_SetColor(LCD_GREY);
    show_text(INFO_X, INFO_Y2, s_line2, buf);
}

/* -------------------------------------------------------------- internals */

/** Redraw a text line only when it actually changed - stops the flicker. */
static void show_text(uint16_t x, uint16_t y, char *cache, const char *text)
{
    if (strncmp(cache, text, TEXT_CACHE_LEN) == 0)
    {
        return;
    }
    strncpy(cache, text, TEXT_CACHE_LEN);
    cache[TEXT_CACHE_LEN] = '\0';
    LCD_DisplayText(x, y, (char *)text);
}

/** Hollow rectangle in the 192x192 preview buffer, clipped to its bounds. */
static void draw_box(uint16_t *buf, int x, int y, int w, int h,
                     uint16_t colour, int thick)
{
    int i, j;
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (w <= 0 || h <= 0)
    {
        return;
    }
    if (x  < 0)          x  = 0;
    if (y  < 0)          y  = 0;
    if (x1 > VIEW_W - 1) x1 = VIEW_W - 1;
    if (y1 > VIEW_H - 1) y1 = VIEW_H - 1;
    if (x1 < x || y1 < y)
    {
        return;
    }

    for (i = 0; i < thick; i++)
    {
        int top = y + i;
        int bot = y1 - i;

        if (top > bot)
        {
            break;
        }
        for (j = x; j <= x1; j++)
        {
            buf[top * VIEW_W + j] = colour;
            buf[bot * VIEW_W + j] = colour;
        }
    }
    for (i = 0; i < thick; i++)
    {
        int left  = x + i;
        int right = x1 - i;

        if (left > right)
        {
            break;
        }
        for (j = y; j <= y1; j++)
        {
            buf[j * VIEW_W + left]  = colour;
            buf[j * VIEW_W + right] = colour;
        }
    }
}

/**
 * @brief Draw every detection into the preview buffer.
 *
 * The strongest face is green, the rest yellow, so it is obvious which box
 * the confidence in the status bar belongs to.  Coordinates come out of the
 * detector in 96x96 space and the preview is 192x192, hence BOX_SCALE.
 */
static void overlay_boxes(const fd_result_t *res)
{
    uint8_t i;

    for (i = 0u; i < res->count && i < FD_MAX_BOXES; i++)
    {
        draw_box(s_disp,
                 (int)res->box[i].x * BOX_SCALE,
                 (int)res->box[i].y * BOX_SCALE,
                 (int)res->box[i].w * BOX_SCALE,
                 (int)res->box[i].h * BOX_SCALE,
                 (i == 0u) ? RGB565_GREEN : RGB565_YELLOW,
                 BOX_THICK);
    }
}

/** Wipe the screen and park a red error message in the middle of it. */
static void fatal_screen(const char *msg)
{
    LCD_SetColor(LCD_RED);
    LCD_Clear();
    LCD_SetAsciiFont(&ASCII_Font24);
    LCD_DisplayText(8, 108, (char *)msg);
    LCD_SetColor(LCD_WHITE);
}
