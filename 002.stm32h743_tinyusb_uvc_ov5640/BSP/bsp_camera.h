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
    CAM_OK         = 0,
    CAM_ERR_I2C    = -1,
    CAM_ERR_ID     = -2,
    CAM_ERR_SENSOR = -3,
    CAM_ERR_DCMI   = -4,
} cam_status_t;

/* Rebuild the capture pipeline if no FRAME interrupt arrives for this long.
 * The sensor runs at >= 8 fps (125 ms), so 500 ms is ~4 missed frames: long
 * enough to never trip on jitter, short enough that a wedge is invisible. */
#define CAM_WATCHDOG_MS 500U

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

/* The camera diagnostic counters that live in the live capture hot-path
 * (frame / error / restart telemetry, test-pattern and sampling-edge control,
 * snapshot coherence diagnostics) are grouped by sub-system in the file-static
 * s_cam_diag structure defined in bsp_camera.c and are reachable only over SWD,
 * e.g.
 *   s_cam_diag.stats.frame_count  capture telemetry
 *   s_cam_diag.ctl.test_pattern   0 = normal, 1 = colour bars
 *   s_cam_diag.ctl.flicker        invert every other sensor frame
 *   s_cam_diag.snap.*             snapshot diagnostics (coherent / torn copies)
 * None of them are referenced from another translation unit.
 *
 * The physical-layer probe groups (pin / bus / pull / regdump / poke) used to
 * share that structure but have been extracted into bsp/sys_prob.c, gated
 * behind the CAM_DIAGNOSTICS macro. Their SWD-readable state is returned by
 * sys_prob_get_state() and the same <group>.req / <group>.done trigger model
 * applies; see bsp/sys_prob.h. */

void bsp_camera_link_dma_callbacks(void);

/* --- Bridge API used by the diagnostic probes (bsp/sys_prob.c) ------------
 * The probes must borrow the live capture pipeline without touching the
 * production data path directly, so they drive it through these public hooks
 * rather than reaching into the file-static camera state. */
void bsp_camera_dvp_pins_mode(bool as_input);
void bsp_camera_dvp_pins_pull(uint32_t pull);
bool bsp_camera_is_auto_running(void);
void bsp_camera_restart_continuous(void);

int32_t bsp_camera_read_reg(uint16_t reg, uint8_t *val);
int32_t bsp_camera_write_reg(uint16_t reg, uint8_t val);

#ifdef __cplusplus
}
#endif

#endif /* BSP_CAMERA_H */
