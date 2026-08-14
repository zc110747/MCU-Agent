/**
  * @file    drv_camera.h
  * @brief   DCMI + OV5640 capture driver for the 192x192 preview page.
  *
  * Pipeline
  * --------
  *   OV5640  320x240 RGB565 (4:3, ISP scaler)
  *      |  DCMI crop, centred
  *      v
  *   192x192 RGB565  -> DMA2_Stream1 double buffer (sram_pool, RAM_D2)
  *      |  snapshot handshake (ISR memcpy into a 3rd buffer)
  *      v
  *   192x192 RGB565  -> LCD_CopyBuffer at the centred 24,24 offset
  *
  * The two DMA target buffers and the third "snapshot" buffer are allocated
  * from the shared sram_pool (RAM_D2) at drv_camera_open() and freed at
  * drv_camera_close(), exactly like the NES machine state / ROM buffers, so
  * the ~221 kB of camera RAM is only occupied while the camera page is open
  * and is returned to the pool for other apps on exit.
  *
  * RAM_D2 is a write-back cacheable region, so every frame the DMA wrote
  * must be invalidated (SCB_InvalidateDCache_by_Addr) before the CPU reads
  * it.  The snapshot buffer is CPU-only, so no maintenance is needed there;
  * LCD_CopyBuffer pushes it over the blocking SPI with no DMA in the path.
  */
#ifndef __DRV_CAMERA_H
#define __DRV_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "drv_camera_ov5640.h"
#include "sram_pool.h"

/* SCCB (I2C4) transport ---------------------------------------------------*/
#define DCMI_DEVICE_ADDRESS         0x78    /* OV5640, already left shifted   */
#define CAM_I2C_MEMADD_SIZE         I2C_MEMADD_SIZE_16BIT
#define CAM_SCCB_TIMEOUT            100

/* Captured frame ----------------------------------------------------------*/
#define CAM_WIDTH                   192
#define CAM_HEIGHT                  192
#define CAM_PIXELS                  (CAM_WIDTH * CAM_HEIGHT)
#define CAM_BYTES                   (CAM_PIXELS * 2)
#define CAM_WORDS                   (CAM_BYTES / 4)

/* Display offset for the centred 192x192 preview on the 240x240 panel. */
#define CAM_DISP_OFFSET             ((240 - CAM_WIDTH) / 2)   /* = 24 */

/* Frame counters kept by the driver. */
extern volatile uint8_t  g_cam_fps;        /* frames per second            */
extern volatile uint32_t g_cam_frames;     /* free running frame counter   */
extern volatile uint32_t g_cam_overruns;   /* DCMI overrun / error count   */
extern volatile uint8_t  g_cam_last_idx;   /* index of last DMA buffer     */

/** Allocate the 3 capture buffers and probe/configure the sensor.
 *  Returns RT_OK / RT_FAIL.  Safe to call only once before drv_camera_start. */
GlobalType_t drv_camera_open(void);

/** Stop capture (if running) and return all buffers to the sram_pool. */
void drv_camera_close(void);

/** Start continuous capture into the internal double buffer. */
GlobalType_t drv_camera_start(void);

/** Stop capture (DMA + DCMI disabled). */
void drv_camera_stop(void);

/**
 * @brief  Re-arm the capture after an overrun was reported.
 */
void drv_camera_recover(void);

/* Snapshot handshake (tear-free preview) ----------------------------------*/
/** Ask the DMA ISR to copy the next completed frame into the snapshot buffer. */
void drv_camera_request_snapshot(void);

/** Returns non-zero once the snapshot buffer holds a fresh frame. */
uint8_t drv_camera_snapshot_ready(void);

/** Pointer to the 192x192 RGB565 snapshot buffer (valid while ready). */
uint16_t *drv_camera_snapshot_ptr(void);

/** Consume the snapshot (clears the ready flag) after blitting it. */
void drv_camera_snapshot_done(void);

/* SCCB register access, used by the OV5640 driver. */
GlobalType_t sccb_read_reg(uint16_t addr, uint8_t *rdata);
GlobalType_t sccb_write_reg(uint16_t addr, uint8_t data);
GlobalType_t sccb_write_buffer(uint16_t addr, uint8_t *pdata, uint16_t size);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_CAMERA_H */
