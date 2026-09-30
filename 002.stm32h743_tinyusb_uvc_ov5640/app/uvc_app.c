/**
 ******************************************************************************
 * @file    uvc_app.c
 * @brief   UVC streaming glue between the DCMI capture engine and TinyUSB.
 *
 * Three 240x240 YUY2 frame buffers live in AXI SRAM at 0x24000000 (the MPU
 * has marked that region non-cacheable, so no cache maintenance is required).
 *
 * Buffer scheme
 * -------------
 *   fb[0]        - live DMA target. The DCMI free-runs and overwrites this
 *                  buffer roughly twice per USB frame transfer.
 *   fb[1], fb[2] - transmit-side pair. One is on the wire, the other collects
 *                  the next snapshot.
 *
 * Handing fb[0] straight to TinyUSB would tear badly: a USB FS isochronous
 * frame needs ~135 ms to shift out 115200 bytes (1023 B per 1 ms microframe)
 * while the sensor rewrites the same memory every ~83 ms underneath it. So we
 * snapshot instead - but *only* during vertical blanking, which is the one
 * phase where fb[0] holds a whole coherent frame. bsp_camera_snapshot()
 * enforces that; copying at an arbitrary phase is what stitched two half
 * frames together whenever the scene moved.
 *
 * Why three buffers and not two: with two, the snapshot could only run while
 * the transmitter was idle, so every frame paid an extra wait for the next
 * blanking window (~27 ms on average, a sixth of the frame rate). The third
 * buffer lets the copy overlap the transfer, so the next frame is always
 * ready the instant the wire frees up.
 ******************************************************************************
 */

#include "uvc_app.h"
#include "bsp_camera.h"
#include "usb_descriptors.h"
#include "tusb.h"

#include <string.h>

#define FB_COUNT 3

/* Placed by the linker script at the start of RAM_D1 == 0x24000000.
 * 3 x 115200 == 337 KB of the 512 KB AXI SRAM. */
__attribute__((section(".framebuffer"), aligned(32))) static uint8_t s_fb[FB_COUNT][FRAME_SIZE];

/* UVC pipeline state machine. Every field is one logical "thing": they are
 * initialised together in uvc_app_init() and reset together in
 * pipeline_reset(). tx_idx / ready_idx use -1 as the "no buffer" sentinel. */
typedef struct
{
    bool     camera_ok;      /* sensor probed OK -> drives the test-pattern path */
    bool     streaming;      /* host selected alternate setting 1 */
    bool     tx_busy;        /* a frame is currently on the wire */
    bool     capture_busy;   /* (reserved) capture-in-progress flag */
    bool     cam_running;    /* mirrors the DCMI/DMA pipeline state */
    int8_t   tx_idx;         /* buffer on the wire, -1 = none */
    int8_t   ready_idx;      /* buffer holding a complete frame, -1 = none */
    uint32_t tx_start_ms;    /* tick when the current transfer started */
    uint32_t cap_start_ms;   /* (reserved) capture start tick */
    uint32_t interval_100ns; /* host-requested frame interval, 100 ns units */
    uint32_t next_frame_ms;  /* tick when the next frame may be sent */

    /* ---- telemetry (was uvc_ and usb_ loose globals, SWD-readable) ---- */
    volatile uint32_t frames_sent;       /* frames handed to USB            */
    volatile uint32_t frames_dropped;    /* frames we had to skip           */
    volatile uint32_t fps_x10;           /* live fps * 10                   */
    volatile uint32_t fps_window_ms;     /* window length for the fps calc  */
    volatile uint32_t fps_frames;        /* frames counted in last window   */
    volatile uint32_t fps_ms;            /* elapsed ms of last window       */
    volatile uint32_t xfer_started;      /* tud_video_n_frame_xfer accepted */
    volatile uint32_t xfer_rejected;     /* tud_video_n_frame_xfer refused  */
    volatile uint32_t tx_timeouts;       /* USB frame never completed       */
    volatile uint32_t cap_timeouts;      /* DCMI frame never completed      */
    volatile uint32_t state;             /* packed flags, see uvc_app_task  */
    volatile uint32_t usb_mounted;       /* USB enumerated / mounted        */
    volatile uint32_t usb_mount_count;   /* mount transitions (debug)       */
    volatile uint32_t usb_suspend_count; /* suspend events (debug)          */
    volatile uint32_t usb_commit_count;  /* commit (SET_CUR) events         */
    volatile uint32_t stream_poll_true;  /* tud_video_n_streaming == true   */
    volatile uint32_t stream_poll_false; /* tud_video_n_streaming == false  */
} uvc_app_t;

static uvc_app_t s_uvc = {
    /* Defaults that must not be zero: the rolling fps estimator only refreshes
     * once per fps_window_ms of wall-clock time, and interval_100ns paces the
     * synthetic test pattern when the sensor is absent. */
    .fps_window_ms  = 1000U,
    .interval_100ns = 10000000UL / FRAME_RATE,
};
/* Rolling frame-rate telemetry. s_uvc.fps_x10 is refreshed once per
 * s_uvc.fps_window_ms of wall-clock time from the s_uvc.frames_sent delta, so the
 * debugger/host reads the *actual* throughput (the USB FS bandwidth bound) with
 * a single SWD access - no external poller stalling the DCMI/USB pipelines.
 * Covers both the live-sensor path and the synthetic test pattern, since both
 * bump s_uvc.frames_sent in tud_video_frame_xfer_complete_cb(). */
/* USB lifecycle telemetry - readable over SWD without a serial port. */
/* Pipeline telemetry - these are what tell a stall apart from a slow host. */
/* A USB frame needs ~122 ms and a DCMI frame ~33 ms. Give both a generous
 * margin: anything past these is a genuine stall, not jitter. */
#define TX_TIMEOUT_MS 600U
#define CAP_TIMEOUT_MS 400U

/* fb[0] is written by the DMA; fb[1]/fb[2] alternate on the wire. */
#define FB_CAPTURE 0
#define FB_TX_A 1
#define FB_TX_B 2

/* Which transmit-side buffer is on the wire, and which holds a complete frame
 * waiting for it. -1 means none. The snapshot always targets the buffer that
 * is *not* s_uvc.tx_idx, so it can run while a transfer is in flight. */

/* Drop everything in flight and start the pipeline over. */
static void pipeline_reset(bool stop_sensor)
{
    if (stop_sensor && s_uvc.camera_ok)
    {
        bsp_camera_stop();
        /* Must be cleared in lockstep with the actual hardware, otherwise the
         * streaming-edge check below thinks capture is still running and never
         * restarts it - which is exactly what happened after every suspend and
         * every probe/commit negotiation. */
        s_uvc.cam_running = false;
    }
    s_uvc.tx_busy      = false;
    s_uvc.capture_busy = false;
    s_uvc.ready_idx    = -1;
    s_uvc.tx_idx       = -1;
}

/* ==========================================================================
 * Synthetic test pattern (used when the OV5640 is missing or failed to init)
 *
 * YUY2 stores two pixels as [Y0 U Y1 V]; the eight classic SMPTE bars below
 * are expressed directly in that colour space.
 * ========================================================================== */
typedef struct {
    uint8_t y, u, v;
} yuv_color_t;

static const yuv_color_t k_bars[8] = {
    {235, 128, 128}, /* white   */
    {210, 16, 146},  /* yellow  */
    {170, 166, 16},  /* cyan    */
    {145, 54, 34},   /* green   */
    {106, 202, 222}, /* magenta */
    {81, 90, 240},   /* red     */
    {41, 240, 110},  /* blue    */
    {16, 128, 128},  /* black   */
};

static void fill_test_pattern(uint8_t *buf, uint32_t phase)
{
    const uint32_t bar_w = FRAME_WIDTH / 8U;

    for (uint32_t y = 0; y < FRAME_HEIGHT; y++)
    {
        uint8_t *row = buf + y * FRAME_WIDTH * 2U;

        /* Scroll the bars horizontally so it is obvious the stream is live. */
        for (uint32_t x = 0; x < FRAME_WIDTH; x += 2U)
        {
            uint32_t           idx = (((x + phase) % FRAME_WIDTH) / bar_w) & 0x7U;
            const yuv_color_t *c   = &k_bars[idx];

            row[x * 2U + 0U] = c->y; /* Y0 */
            row[x * 2U + 1U] = c->u; /* U  */
            row[x * 2U + 2U] = c->y; /* Y1 */
            row[x * 2U + 3U] = c->v; /* V  */
        }

        /* A moving horizontal marker line makes frame drops easy to spot. */
        if (y == (phase % FRAME_HEIGHT))
        {
            for (uint32_t x = 0; x < FRAME_WIDTH * 2U; x += 4U)
            {
                row[x + 0U] = 235;
                row[x + 1U] = 128;
                row[x + 2U] = 235;
                row[x + 3U] = 128;
            }
        }
    }
}

/* ==========================================================================
 * Public API
 * ========================================================================== */
void uvc_app_init(bool camera_ok)
{
    s_uvc.camera_ok = camera_ok;

    memset(s_fb, 0, sizeof(s_fb));

    s_uvc.streaming    = false;
    s_uvc.tx_busy      = false;
    s_uvc.capture_busy = false;
    s_uvc.ready_idx    = -1;
    s_uvc.tx_idx       = -1;
    s_uvc.cam_running  = false;
    s_uvc.tx_start_ms  = 0;
    s_uvc.cap_start_ms = 0;

    if (s_uvc.camera_ok)
    {
        bsp_camera_set_buffers(s_fb);
        /* Do NOT start continuous capture here — wait for the host to select
         * alt-setting 1. Starting early would fill buffers that nobody reads
         * and waste DMA bandwidth. */
    }
}

bool uvc_app_is_streaming(void)
{
    return s_uvc.streaming;
}

void uvc_app_task(void)
{
    static uint32_t phase = 0;
    const uint32_t  now   = HAL_GetTick();

    /* One packed word makes the whole pipeline state visible in a single SWD
     * read, which beats guessing from counters alone. */
    s_uvc.state = (s_uvc.streaming ? 0x01U : 0U) |
                  (s_uvc.tx_busy ? 0x02U : 0U) |
                  (s_uvc.capture_busy ? 0x04U : 0U) |
                  ((s_uvc.ready_idx >= 0) ? 0x08U : 0U) |
                  (s_uvc.camera_ok ? 0x10U : 0U) |
                  (s_uvc.cam_running ? 0x20U : 0U);

    /* ---- 0. Rolling frame-rate estimate ----
     * Accumulate sent frames over a sliding wall-clock window and publish the
     * result as s_uvc.fps_x10, so a single SWD read reports the *actual* throughput
     * rather than the announced FRAME_RATE. The first window after boot may mix
     * pre-stream samples (frames == 0 -> 0 fps), then it tracks live within one
     * s_uvc.fps_window_ms. */
    {
        static uint32_t s_fps_base_sent = 0U;
        static uint32_t s_fps_base_ms   = 0U;
        const uint32_t  dt              = now - s_fps_base_ms;

        if (dt >= s_uvc.fps_window_ms)
        {
            const uint32_t frames = s_uvc.frames_sent - s_fps_base_sent;
            /* fps * 10 = frames * 10000 / dt_ms  (10000 == 10 * 1000). */
            s_uvc.fps_x10    = (frames * 10000U) / dt;
            s_uvc.fps_frames = frames;
            s_uvc.fps_ms     = dt;
            s_fps_base_sent  = s_uvc.frames_sent;
            s_fps_base_ms    = now;
        }
    }

    /* ---- 1. Track the host-driven streaming state ---- */
    bool host_streaming = tud_video_n_streaming(0, 0);
    if (host_streaming)
        s_uvc.stream_poll_true++;
    else
        s_uvc.stream_poll_false++;

    if (host_streaming != s_uvc.streaming)
    {
        s_uvc.streaming = host_streaming;

        if (!s_uvc.streaming)
        {
            pipeline_reset(true); /* host closed the stream */
        }
        else
        {
            pipeline_reset(false);
            s_uvc.next_frame_ms = now;
        }
    }

    if (!s_uvc.streaming)
    {
        return;
    }

    /* ---- 2. Watchdogs ----
     * An isochronous transfer that never completes (bus suspended mid-frame) would
     * otherwise wedge the pipeline forever, since it is gated on a callback that
     * is simply never going to fire. Time it out and rebuild the pipeline.
     *
     * In continuous mode there is no per-frame capture timeout: frames arrive on
     * their own schedule and we just pick them up when we can. */
    if (s_uvc.tx_busy && (uint32_t)(now - s_uvc.tx_start_ms) > TX_TIMEOUT_MS)
    {
        s_uvc.tx_timeouts++;
        s_uvc.frames_dropped++;
        s_uvc.tx_busy = false;
    }

    /* ---- 3. Keep the capture pipeline alive ----
     * Started here rather than at the end of the task so that the very first
     * streaming pass already has the DCMI running. */
    if (s_uvc.camera_ok)
    {
        if (!s_uvc.cam_running)
        {
            if (bsp_camera_start_continuous(s_fb) == CAM_OK)
            {
                s_uvc.cam_running = true;
            }
        }
        /* bsp_camera_service() itself runs from the super-loop in main.c so that
         * the debugger hooks stay live even while the host is not streaming. */
    }

    /* ---- 4. Acquire a frame ----
     * Always into the transmit-side buffer that is not on the wire, so this can
     * run concurrently with a transfer. bsp_camera_snapshot() returns false
     * unless the sensor is in vertical blanking, so most passes do nothing and
     * we simply try again - which is exactly the point: the copy is pinned to
     * the one phase where the capture buffer is coherent. */
    const uint8_t dst = (s_uvc.tx_idx == FB_TX_A) ? FB_TX_B : FB_TX_A;

    if (s_uvc.camera_ok)
    {
        /* Deliberately unconditional: re-snapshotting on every blanking interval
         * keeps the pending frame as fresh as possible, and overwriting a frame
         * that has not gone out yet costs nothing but a memcpy. */
        if (bsp_camera_snapshot(s_fb[dst]))
        {
            s_uvc.ready_idx = (int8_t)dst;
        }
    }
    else
    {
        /* No sensor: synthesise a frame, paced by the negotiated interval. */
        if ((s_uvc.ready_idx < 0) && (int32_t)(now - s_uvc.next_frame_ms) >= 0)
        {
            fill_test_pattern(s_fb[dst], phase);
            phase += 4;
            s_uvc.ready_idx = (int8_t)dst;
        }
    }

    /* ---- 5. Hand the completed frame to TinyUSB ---- */
    if ((s_uvc.ready_idx >= 0) && !s_uvc.tx_busy)
    {
        if (tud_video_n_frame_xfer(0, 0, s_fb[s_uvc.ready_idx], FRAME_SIZE))
        {
            s_uvc.tx_idx      = s_uvc.ready_idx;
            s_uvc.ready_idx   = -1;
            s_uvc.tx_busy     = true;
            s_uvc.tx_start_ms = now;
            s_uvc.xfer_started++;

            /* Interval is in 100 ns units; convert to milliseconds. */
            s_uvc.next_frame_ms = now + (s_uvc.interval_100ns / 10000UL);
        }
        else
        {
            s_uvc.xfer_rejected++;
        }
    }
}

/* ==========================================================================
 * TinyUSB video class callbacks
 * ========================================================================== */

/* ---- Device lifecycle ---- */
void tud_mount_cb(void)
{
    s_uvc.usb_mounted = 1;
    s_uvc.usb_mount_count++;
}

void tud_umount_cb(void)
{
    s_uvc.usb_mounted = 0;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    s_uvc.usb_mounted = 0;
    s_uvc.usb_suspend_count++;

    /* Any isochronous transfer in flight dies with the bus. Clearing the flags
     * here means we do not have to wait out the watchdog after a resume. */
    pipeline_reset(true);
}

void tud_resume_cb(void)
{
    s_uvc.usb_mounted = 1;
}

/* Called once the whole frame has been shifted out to the host. */
void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx)
{
    (void)ctl_idx;
    (void)stm_idx;

    s_uvc.tx_busy = false;
    s_uvc.frames_sent++;
}

/* Called when the host commits a probe/commit negotiation (VS_COMMIT_CONTROL). */
int tud_video_commit_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx,
                        video_probe_and_commit_control_t const *parameters)
{
    (void)ctl_idx;
    (void)stm_idx;

    s_uvc.usb_commit_count++;

    if (parameters->dwFrameInterval != 0U)
    {
        s_uvc.interval_100ns = parameters->dwFrameInterval;
    }

    /* Restart the pipeline for the new settings. */
    pipeline_reset(true);
    s_uvc.next_frame_ms = HAL_GetTick();

    return VIDEO_ERROR_NONE;
}
