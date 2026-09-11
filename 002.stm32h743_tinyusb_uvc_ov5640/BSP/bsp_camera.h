/**
 ******************************************************************************
 * @file    bsp_camera.h
 * @brief   OV5640 (I2C4 / SCCB) + DCMI + DMA capture front-end.
 *
 * Usage (continuous mode):
 *   1. bsp_camera_init()          — power up, probe, configure
 *   2. bsp_camera_set_buffers()   — tell the driver where the ping-pong bufs live
 *   3. bsp_camera_start_continuous() — DCMI+DMA run forever
 *   4. loop: buf = bsp_camera_take_frame()  -> hand to USB -> bsp_camera_advance()
 *   5. bsp_camera_stop()          — when the host closes the stream
 ******************************************************************************
 */

#ifndef BSP_CAMERA_H
#define BSP_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#include <stdbool.h>

typedef enum {
  CAM_OK = 0,
  CAM_ERR_I2C     = -1,
  CAM_ERR_ID      = -2,
  CAM_ERR_SENSOR  = -3,
  CAM_ERR_DCMI    = -4,
} cam_status_t;

/* Rebuild the capture pipeline if no FRAME interrupt arrives for this long.
 * The sensor runs at >= 8 fps (125 ms), so 500 ms is ~4 missed frames: long
 * enough to never trip on jitter, short enough that a wedge is invisible. */
#define CAM_WATCHDOG_MS  500U

/* Handles are exported so the interrupt vectors can reach them. */
extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef  hdma_dcmi;
extern I2C_HandleTypeDef  hi2c_cam;

/* Power up the sensor, probe its ID, configure QVGA/YUV422 and set up the
 * DCMI crop window so that each capture yields a 240x240 YUY2 frame. */
cam_status_t bsp_camera_init(void);

/* Read back the sensor ID (0x5640 when everything is wired correctly). */
uint32_t bsp_camera_get_id(void);

/* Register the two frame buffers used for continuous double-buffer DMA.
 * Must be called before bsp_camera_start_continuous(). Each buffer must be
 * at least FRAME_SIZE bytes and 32-byte aligned. */
void bsp_camera_set_buffers(uint8_t (*fb)[FRAME_SIZE]);

/* Start continuous DCMI + circular DMA into buf[0]. After this call frames
 * arrive autonomously; pull them out with bsp_camera_snapshot(). */
cam_status_t bsp_camera_start_continuous(uint8_t (*buf)[FRAME_SIZE]);

/* Abort continuous capture. Safe to call even if not started. */
void bsp_camera_stop(void);

/* Main-loop housekeeping: drains the polled OVR / sync-error flags and
 * restarts the pipeline if the sensor stops delivering frames. Call this on
 * every pass of the super-loop while capture is running. */
void bsp_camera_service(void);

/* Non-blocking: returns a pointer to the most recently completed frame, or
 * NULL if no new frame has landed since the last advance(). The pointer is
 * valid until the next call or until a new frame completes. */
const uint8_t *bsp_camera_take_frame(void);

/* Mark the current frame as consumed. Call once per successful take_frame()
 * cycle to keep the producer/consumer indices in sync. */
void bsp_camera_advance(void);

/* Copy the most recent complete frame into dst, but only while the sensor is
 * in vertical blanking - the single phase at which the circular capture
 * buffer holds one whole coherent frame. Returns false without touching dst
 * if no new frame is pending, if the blanking window is not open yet (just
 * retry on the next pass), or if the DMA caught up mid-copy.
 *
 * This is what keeps fast motion from arriving at the host as two half
 * frames stitched together; see the comment block at the implementation. */
bool bsp_camera_snapshot(uint8_t *dst);

/* True while the DCMI sits between frames. Exposed for diagnostics. */
bool bsp_camera_in_vblank(void);

/* Snapshot telemetry - readable over SWD.
 *   cam_snap_ok    coherent frames delivered
 *   cam_snap_wait  polls deferred because blanking had not started
 *   cam_snap_torn  copies discarded because the DMA overtook them
 *   cam_snap_ndtr0 / ndtr1  DMA counter either side of the last copy
 *   cam_snap_cycles         CPU cycles the last copy took (480 MHz clock) */
/* Debug hooks for exercising the tear-free path without a USB host:
 *   cam_snap_test  1 = snapshot into the first transmit buffer every pass
 *   cam_flicker    1 = invert every other sensor frame, so that any snapshot
 *                      spanning two frames shows a full-scale luma step */
extern volatile uint32_t cam_snap_test;
extern volatile uint32_t cam_flicker;
extern volatile uint32_t cam_snap_unsync;
extern volatile uint32_t cam_unsync_ndtr;

/* Generic SCCB poke: set cam_poke_reg / cam_poke_val, then cam_poke_req = 1;
 * cam_poke_done increments once the write has gone out. */
extern volatile uint32_t cam_poke_req;
extern volatile uint32_t cam_poke_reg;
extern volatile uint32_t cam_poke_val;
extern volatile uint32_t cam_poke_done;

/* Which register cam_flicker alternates, and between which two values. */
extern volatile uint32_t cam_flicker_reg;
extern volatile uint32_t cam_flicker_a;
extern volatile uint32_t cam_flicker_b;

extern volatile uint32_t cam_snap_ok;
extern volatile uint32_t cam_snap_wait;
extern volatile uint32_t cam_snap_torn;
extern volatile uint32_t cam_snap_ndtr0;
extern volatile uint32_t cam_snap_ndtr1;
extern volatile uint32_t cam_snap_cycles;

/* Wire the M0/M1 DMA callbacks into hdma_dcmi. Call after HAL_DCMI_Init. */
void bsp_camera_link_dma_callbacks(void);

/* ---- Sensor diagnostics -------------------------------------------------
 * cam_reg_val[] mirrors the DVP-relevant OV5640 registers listed in
 * cam_reg_addr[], refreshed at init and whenever cam_test_pattern changes.
 * Write cam_test_pattern from the debugger (0 = normal, 1 = colour bars) to
 * decide whether a bad image comes from the sensor or from the DVP link. */
#define CAM_REG_SNAP_N 18U

extern const uint16_t   cam_reg_addr[CAM_REG_SNAP_N];
extern volatile uint8_t cam_reg_val[CAM_REG_SNAP_N];
extern volatile uint32_t cam_test_pattern;
extern volatile uint32_t cam_pclk_pol; /* 1 = sample on rising PIXCLK, 0 = falling */

/* Physical-layer probe. Set cam_probe_req = 1 from the debugger; the result
 * lands in cam_pin_ones / cam_pin_zeros / cam_pin_edges when cam_probe_done
 * increments. Bits 0..7 = D0..D7, 8 = HSYNC, 9 = VSYNC, 10 = PIXCLK. */
extern volatile uint32_t cam_crop_en;         /* 1 = 240x240 centre crop, 0 = full line */
extern volatile uint32_t cam_words_per_frame; /* DMA words between FRAME IRQs           */

extern volatile uint32_t cam_probe_req;
extern volatile uint32_t cam_pin_ones;
extern volatile uint32_t cam_pin_zeros;
extern volatile uint32_t cam_pin_edges[11];
extern volatile uint32_t cam_probe_done;

/* Data-bus probe bucketed by HSYNC state. Set cam_href_req = 1; results land
 * when cam_probe_done increments.
 *   cam_bus_hi_* : sampled while HSYNC high   (active line as the probe sees it)
 *   cam_bus_lo_* : sampled while HSYNC low    (blanking as the probe sees it)
 * Both buckets constant => sensor not streaming pixel data. Only one bucket
 * varying => the DCMI HSPolarity selects the wrong window. */
extern volatile uint32_t cam_href_req;
extern volatile uint32_t cam_force_run;  /* 1 = run capture with no USB host */
extern volatile uint8_t  cam_bus_hi_samples[16];
extern volatile uint8_t  cam_bus_lo_samples[16];
extern volatile uint32_t cam_bus_hi_distinct;
extern volatile uint32_t cam_bus_lo_distinct;
extern volatile uint32_t cam_bus_hi_count;
extern volatile uint32_t cam_bus_lo_count;
extern volatile uint32_t cam_href_edges;
extern volatile uint32_t cam_vsync_edges;

/* Raw DVP bus trace. Set cam_trace_req = 1; when cam_trace_done increments,
 * cam_trace[] holds 4096 interleaved GPIOA / GPIOE IDR words captured at full
 * load bandwidth starting from an HSYNC rising edge:
 *   even slots  GPIOA->IDR : bit4 = HSYNC (PA4),  bit6 = PIXCLK (PA6)
 *   odd  slots  GPIOE->IDR : bit4 = D4 (PE4), bit5 = D6 (PE5), bit6 = D7 (PE6)
 * Decode with debug/bustrace.py. */
#define CAM_TRACE_N 4096U

extern volatile uint32_t cam_trace_req;
extern volatile uint32_t cam_trace_done;
extern uint32_t          cam_trace[CAM_TRACE_N];

void bsp_camera_trace_bus(void);

/* Pull-resistor discrimination: tells a driven line from a disconnected one.
 * Set cam_pull_req = 1; when cam_pull_done increments, cam_pull_and[] /
 * cam_pull_or[] hold the accumulated samples for index 0 = no pull,
 * 1 = pull-up, 2 = pull-down (bit order as in cam_pin_ones). A line whose
 * value tracks the pull is not connected to the sensor. */
extern volatile uint32_t cam_pull_req;
extern volatile uint32_t cam_pull_done;
extern volatile uint32_t cam_pull_and[3];
extern volatile uint32_t cam_pull_or[3];

void bsp_camera_probe_pull(void);

/* Arbitrary SCCB register window: set cam_regdump_base, then cam_regdump_req
 * = 1. When cam_regdump_done increments, cam_regdump[] holds 64 consecutive
 * registers starting at the base (0xEE = read failed). */
#define CAM_REGDUMP_N 64U

extern volatile uint16_t cam_regdump_base;
extern volatile uint32_t cam_regdump_req;
extern volatile uint32_t cam_regdump_done;
extern volatile uint8_t  cam_regdump[CAM_REGDUMP_N];

void bsp_camera_regdump(void);

void bsp_camera_probe_pins(void);
void bsp_camera_probe_href(void);

void    cam_snapshot_regs(void);
int32_t bsp_camera_read_reg(uint16_t reg, uint8_t *val);
int32_t bsp_camera_write_reg(uint16_t reg, uint8_t val);

/* Capture telemetry - readable over SWD without a serial port. */
extern volatile uint32_t cam_frame_count;      /* DCMI frame IRQs             */
extern volatile uint32_t cam_error_count;      /* DMA-level errors            */
extern volatile uint32_t cam_start_count;      /* accepted capture starts     */
extern volatile uint32_t cam_start_fail_count; /* rejected capture starts     */
extern volatile uint32_t cam_ovr_count;        /* DCMI FIFO overruns (polled) */
extern volatile uint32_t cam_sync_err_count;   /* sync errors (polled)        */
extern volatile uint32_t cam_restart_count;    /* watchdog pipeline restarts  */
extern volatile uint32_t cam_last_frame_ms;    /* tick of the last FRAME IRQ  */

#ifdef __cplusplus
}
#endif

#endif /* BSP_CAMERA_H */
