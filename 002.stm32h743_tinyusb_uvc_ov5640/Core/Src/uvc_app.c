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
__attribute__((section(".framebuffer"), aligned(32)))
static uint8_t s_fb[FB_COUNT][FRAME_SIZE];

volatile uint32_t uvc_frames_sent    = 0;
volatile uint32_t uvc_frames_dropped = 0;

/* USB lifecycle telemetry - readable over SWD without a serial port. */
volatile uint32_t usb_mounted       = 0; /* 1 while the host has us configured */
volatile uint32_t usb_mount_count   = 0; /* number of successful enumerations  */
volatile uint32_t usb_suspend_count = 0;
volatile uint32_t usb_commit_count  = 0; /* VS_COMMIT_CONTROL requests handled */

/* Pipeline telemetry - these are what tell a stall apart from a slow host. */
volatile uint32_t uvc_xfer_started  = 0; /* tud_video_n_frame_xfer() accepted  */
volatile uint32_t uvc_xfer_rejected = 0; /* tud_video_n_frame_xfer() refused   */
volatile uint32_t uvc_tx_timeouts   = 0; /* USB frame never completed          */
volatile uint32_t uvc_cap_timeouts  = 0; /* DCMI frame never completed         */
volatile uint32_t uvc_state         = 0; /* packed flags, see uvc_app_task()   */
volatile uint32_t uvc_stream_poll_true  = 0; /* tud_video_n_streaming == true      */
volatile uint32_t uvc_stream_poll_false = 0; /* tud_video_n_streaming == false     */

/* A USB frame needs ~122 ms and a DCMI frame ~33 ms. Give both a generous
 * margin: anything past these is a genuine stall, not jitter. */
#define TX_TIMEOUT_MS   600U
#define CAP_TIMEOUT_MS  400U

/* fb[0] is written by the DMA; fb[1]/fb[2] alternate on the wire. */
#define FB_CAPTURE 0
#define FB_TX_A    1
#define FB_TX_B    2

static bool s_camera_ok    = false;
static bool s_streaming    = false;
static bool s_tx_busy      = false;
static bool s_capture_busy = false;
static bool s_cam_running  = false; /* mirrors the DCMI/DMA pipeline state */

/* Which transmit-side buffer is on the wire, and which holds a complete frame
 * waiting for it. -1 means none. The snapshot always targets the buffer that
 * is *not* s_tx_idx, so it can run while a transfer is in flight. */
static int8_t s_tx_idx    = -1;
static int8_t s_ready_idx = -1;

static uint32_t s_tx_start_ms  = 0;
static uint32_t s_cap_start_ms = 0;

/* Interval requested by the host, in 100 ns units (default 1/8 s). */
static uint32_t s_interval_100ns = 10000000UL / FRAME_RATE;
static uint32_t s_next_frame_ms  = 0;

/* Drop everything in flight and start the pipeline over. */
static void pipeline_reset(bool stop_sensor)
{
  if (stop_sensor && s_camera_ok) {
    bsp_camera_stop();
    /* Must be cleared in lockstep with the actual hardware, otherwise the
     * streaming-edge check below thinks capture is still running and never
     * restarts it - which is exactly what happened after every suspend and
     * every probe/commit negotiation. */
    s_cam_running = false;
  }
  s_tx_busy      = false;
  s_capture_busy = false;
  s_ready_idx    = -1;
  s_tx_idx       = -1;
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
    {210,  16, 146}, /* yellow  */
    {170, 166,  16}, /* cyan    */
    {145,  54,  34}, /* green   */
    {106, 202, 222}, /* magenta */
    { 81,  90, 240}, /* red     */
    { 41, 240, 110}, /* blue    */
    { 16, 128, 128}, /* black   */
};

static void fill_test_pattern(uint8_t *buf, uint32_t phase)
{
  const uint32_t bar_w = FRAME_WIDTH / 8U;

  for (uint32_t y = 0; y < FRAME_HEIGHT; y++) {
    uint8_t *row = buf + y * FRAME_WIDTH * 2U;

    /* Scroll the bars horizontally so it is obvious the stream is live. */
    for (uint32_t x = 0; x < FRAME_WIDTH; x += 2U) {
      uint32_t idx = (((x + phase) % FRAME_WIDTH) / bar_w) & 0x7U;
      const yuv_color_t *c = &k_bars[idx];

      row[x * 2U + 0U] = c->y; /* Y0 */
      row[x * 2U + 1U] = c->u; /* U  */
      row[x * 2U + 2U] = c->y; /* Y1 */
      row[x * 2U + 3U] = c->v; /* V  */
    }

    /* A moving horizontal marker line makes frame drops easy to spot. */
    if (y == (phase % FRAME_HEIGHT)) {
      for (uint32_t x = 0; x < FRAME_WIDTH * 2U; x += 4U) {
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
  s_camera_ok = camera_ok;

  memset(s_fb, 0, sizeof(s_fb));

  s_streaming    = false;
  s_tx_busy      = false;
  s_capture_busy = false;
  s_ready_idx    = -1;
  s_tx_idx       = -1;
  s_cam_running  = false;
  s_tx_start_ms  = 0;
  s_cap_start_ms = 0;

  if (s_camera_ok) {
    bsp_camera_set_buffers(s_fb);
    /* Do NOT start continuous capture here — wait for the host to select
     * alt-setting 1. Starting early would fill buffers that nobody reads
     * and waste DMA bandwidth. */
  }
}

bool uvc_app_is_streaming(void)
{
  return s_streaming;
}

void uvc_app_task(void)
{
  static uint32_t phase = 0;
  const uint32_t now = HAL_GetTick();

  /* One packed word makes the whole pipeline state visible in a single SWD
   * read, which beats guessing from counters alone. */
  uvc_state = (s_streaming        ? 0x01U : 0U) |
              (s_tx_busy          ? 0x02U : 0U) |
              (s_capture_busy     ? 0x04U : 0U) |
              ((s_ready_idx >= 0) ? 0x08U : 0U) |
              (s_camera_ok        ? 0x10U : 0U) |
              (s_cam_running      ? 0x20U : 0U);

  /* ---- 1. Track the host-driven streaming state ---- */
  bool host_streaming = tud_video_n_streaming(0, 0);
  if (host_streaming) uvc_stream_poll_true++; else uvc_stream_poll_false++;

  if (host_streaming != s_streaming) {
    s_streaming = host_streaming;

    if (!s_streaming) {
      pipeline_reset(true); /* host closed the stream */
    } else {
      pipeline_reset(false);
      s_next_frame_ms = now;
    }
  }

  if (!s_streaming) {
    return;
  }

  /* ---- 2. Watchdogs ----
   * An isochronous transfer that never completes (bus suspended mid-frame) would
   * otherwise wedge the pipeline forever, since it is gated on a callback that
   * is simply never going to fire. Time it out and rebuild the pipeline.
   *
   * In continuous mode there is no per-frame capture timeout: frames arrive on
   * their own schedule and we just pick them up when we can. */
  if (s_tx_busy && (uint32_t)(now - s_tx_start_ms) > TX_TIMEOUT_MS) {
    uvc_tx_timeouts++;
    uvc_frames_dropped++;
    s_tx_busy = false;
  }

  /* ---- 3. Keep the capture pipeline alive ----
   * Started here rather than at the end of the task so that the very first
   * streaming pass already has the DCMI running. */
  if (s_camera_ok) {
    if (!s_cam_running) {
      if (bsp_camera_start_continuous(s_fb) == CAM_OK) {
        s_cam_running = true;
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
  const uint8_t dst = (s_tx_idx == FB_TX_A) ? FB_TX_B : FB_TX_A;

  if (s_camera_ok) {
    /* Deliberately unconditional: re-snapshotting on every blanking interval
     * keeps the pending frame as fresh as possible, and overwriting a frame
     * that has not gone out yet costs nothing but a memcpy. */
    if (bsp_camera_snapshot(s_fb[dst])) {
      s_ready_idx = (int8_t)dst;
    }
  } else {
    /* No sensor: synthesise a frame, paced by the negotiated interval. */
    if ((s_ready_idx < 0) && (int32_t)(now - s_next_frame_ms) >= 0) {
      fill_test_pattern(s_fb[dst], phase);
      phase += 4;
      s_ready_idx = (int8_t)dst;
    }
  }

  /* ---- 5. Hand the completed frame to TinyUSB ---- */
  if ((s_ready_idx >= 0) && !s_tx_busy) {
    if (tud_video_n_frame_xfer(0, 0, s_fb[s_ready_idx], FRAME_SIZE)) {
      s_tx_idx      = s_ready_idx;
      s_ready_idx   = -1;
      s_tx_busy     = true;
      s_tx_start_ms = now;
      uvc_xfer_started++;

      /* Interval is in 100 ns units; convert to milliseconds. */
      s_next_frame_ms = now + (s_interval_100ns / 10000UL);
    } else {
      uvc_xfer_rejected++;
    }
  }
}

/* ==========================================================================
 * TinyUSB video class callbacks
 * ========================================================================== */

/* ---- Device lifecycle ---- */
void tud_mount_cb(void)
{
  usb_mounted = 1;
  usb_mount_count++;
}

void tud_umount_cb(void)
{
  usb_mounted = 0;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
  (void)remote_wakeup_en;
  usb_mounted = 0;
  usb_suspend_count++;

  /* Any isochronous transfer in flight dies with the bus. Clearing the flags
   * here means we do not have to wait out the watchdog after a resume. */
  pipeline_reset(true);
}

void tud_resume_cb(void)
{
  usb_mounted = 1;
}

/* Called once the whole frame has been shifted out to the host. */
void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx)
{
  (void)ctl_idx;
  (void)stm_idx;

  s_tx_busy = false;
  uvc_frames_sent++;
}

/* Called when the host commits a probe/commit negotiation (VS_COMMIT_CONTROL). */
int tud_video_commit_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx,
                        video_probe_and_commit_control_t const *parameters)
{
  (void)ctl_idx;
  (void)stm_idx;

  usb_commit_count++;

  if (parameters->dwFrameInterval != 0U) {
    s_interval_100ns = parameters->dwFrameInterval;
  }

  /* Restart the pipeline for the new settings. */
  pipeline_reset(true);
  s_next_frame_ms = HAL_GetTick();

  return VIDEO_ERROR_NONE;
}
