/**
 * @file bsp_cam.h
 * @brief Camera subsystem (Phase 3): GPT7 XCLK + SCCB probe + OV5640 + CEU
 *
 * Replicates the official vision_board_camera bring-up order:
 *   1. GPT7 PWM on P1011 -> 24 MHz XCLK (PCLKA 120 MHz / period 5, duty 40%)
 *      NOTE: P1011 is the RGB backlight line - during camera operation the
 *      backlight runs on the same PWM (about 40% brightness). Official
 *      coexistence status quo, see documents/phase3-report.md.
 *   2. PWDN(P705)/RESET(P704) power sequence
 *   3. SCCB bus scan 0x15..0x77, sensor ID detection
 *   4. OV5640: default_regs + QVGA RGB565 window
 *   5. CEU open (IRQ slot 1, EVENT_CEU_CEUI) + snapshot on demand
 *
 * The frame buffer lands in .nocache_sdram (right after the 768000-byte LCD
 * framebuffer, no overlap - linker places sections sequentially).
 */
#ifndef BSP_CAM_H_
#define BSP_CAM_H_

#include "bsp_api.h"
#include "bsp_ov5640.h"

#define BSP_CAM_WIDTH       (320U)
#define BSP_CAM_HEIGHT      (240U)
#define BSP_CAM_BPP         (2U)
#define BSP_CAM_FRAME_BYTES (BSP_CAM_WIDTH * BSP_CAM_HEIGHT * BSP_CAM_BPP)

/** One bus scan outcome (first ACKing address + best-effort identification). */
typedef struct
{
    bool     present;   /* any device ACKed on the bus                 */
    uint8_t  addr7;     /* first ACKing 7-bit address                  */
    uint16_t id;        /* raw id read from the identification attempt */
    const char * name;  /* sensor name or "unknown"                    */
} bsp_cam_scan_t;

/** Full bring-up (idempotent). Returns FSP_SUCCESS when ready to snap. */
fsp_err_t bsp_cam_init (void);

/** True after a successful bsp_cam_init(). */
bool bsp_cam_ready (void);

/** Bus scan + identification without re-initializing anything. */
fsp_err_t bsp_cam_scan (bsp_cam_scan_t * p_out);

/** Capture one frame into the internal buffer (blocking, 200 ms timeout). */
fsp_err_t bsp_cam_snap (void);

/** Internal frame buffer (320x240 RGB565, .nocache_sdram). */
uint8_t * bsp_cam_framebuffer (void);

/** OV5640 colorbar test pattern on/off. */
fsp_err_t bsp_cam_colorbar (bool enable);

#endif /* BSP_CAM_H_ */
