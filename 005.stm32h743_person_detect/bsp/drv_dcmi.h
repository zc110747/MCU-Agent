/**
 * @file    drv_dcmi.h
 * @brief   DCMI + OV5640 capture driver with a true hardware double buffer.
 *
 * Pipeline
 * --------
 *   OV5640  320x240 RGB565 (4:3, ISP scaler)
 *      |  DCMI crop, centred
 *      v
 *   192x192 RGB565  -> DMA2_Stream7 double buffer @ 0x24000000 (non-cacheable)
 *      |  2x box downsample + luma
 *      v
 *   96x96 int8 grey -> CMSIS-NN person detection
 *
 * The two DMA target buffers live in the .dma_buffer section, which the MPU
 * marks as non-cacheable so no cache maintenance is needed after a frame.
 */
#ifndef __DRV_DCMI_H
#define __DRV_DCMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "drv_dcmi_ov5640.h"

#define DCMI_DEVICE_OV5640          1
#define DCMI_USE_DEVICE             DCMI_DEVICE_OV5640

/* SCCB (I2C4) transport ---------------------------------------------------*/
#define DCMI_DEVICE_ADDRESS         0x78    /* OV5640, already left shifted   */
#define I2C_MEMADD_SIZE             I2C_MEMADD_SIZE_16BIT
#define DCMI_TIMEOUT                100

/* Captured frame ----------------------------------------------------------*/
#define CAPTURE_WIDTH               192
#define CAPTURE_HEIGHT              192
#define CAPTURE_PIXELS              (CAPTURE_WIDTH * CAPTURE_HEIGHT)
#define CAPTURE_BYTES               (CAPTURE_PIXELS * 2)
#define CAPTURE_WORDS               (CAPTURE_BYTES / 4)

/* Frame counters kept by the driver. */
extern volatile uint8_t  g_dcmi_fps;        /* frames per second            */
extern volatile uint32_t g_dcmi_frames;     /* free running frame counter   */
extern volatile uint32_t g_dcmi_overruns;   /* DCMI overrun / error count   */
extern volatile uint8_t  g_dcmi_last_idx;   /* index of last consumed buffer*/

GlobalType_t drv_dcmi_init(void);

/** Start continuous capture into the internal double buffer. */
GlobalType_t drv_dcmi_start(void);

/** Stop capture (DMA + DCMI disabled). */
void drv_dcmi_stop(void);

/**
 * @brief  Non blocking check for a freshly completed frame.
 * @param  frame  receives a pointer to the buffer that DMA is *not* writing.
 * @return RT_OK when a new frame is available, RT_FAIL otherwise.
 *
 * The returned buffer stays valid until the next-but-one frame completes,
 * so the caller must copy it (or finish with it) within one frame period.
 */
GlobalType_t drv_dcmi_get_frame(uint16_t **frame);

/** Re-arm the capture after an overrun was reported. */
void drv_dcmi_recover(void);

/* SCCB register access, used by the OV5640 driver. */
GlobalType_t sccb_read_reg(uint16_t addr, uint8_t *rdata);
GlobalType_t sccb_write_reg(uint16_t addr, uint8_t data);
GlobalType_t sccb_write_buffer(uint16_t addr, uint8_t *pdata, uint16_t size);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_DCMI_H */
