/* ---------------------------------------------------------------------------
 * bsp_camera.h - OV5640 + DCMI capture front end for the motion sensor.
 *
 * Data path:
 *   OV5640 --(8-bit DVP, YUV422/YUYV, 400x300)--> DCMI --crop 160x120-->
 *   DMA2_Stream1 (CIRCULAR over TWO frame buffers) --> D2 SRAM
 *
 * Why two buffers and not one
 *     The consumer needs a whole frame while the producer is already filling
 *     the next one. A single circular buffer means the DMA is always writing
 *     into the frame the CPU is reading, which shows up as a diagonal tear -
 *     a wrong displacement, not just a cosmetic glitch.
 *
 * Why the DMA is CIRCULAR and ERR/OVR interrupts stay off
 *     The DCMI runs in CONTINUOUS mode and never stops feeding the FIFO.
 *     A NORMAL-mode stream disables itself after one transfer, leaving the
 *     DCMI nowhere to drain -> overrun -> HAL_DCMI_IRQHandler() aborts the DMA
 *     and parks the handle in HAL_DCMI_STATE_ERROR, so one glitch kills the
 *     stream for good. OVR/ERR are polled in bsp_camera_service() instead,
 *     where a glitch costs one frame.
 *
 * Memory
 *     .cam_buf, D2 SRAM (0x30000000) - reachable by DMA2, NOT by DMA1/2's
 *     sibling DTCM. The section is NOLOAD, so no cache line of it is ever
 *     dirty at the first DMA and a plain invalidate cannot lose data.
 *
 * Busy-state contract
 *     bsp_camera_acquire() copies the completed frame into a private work
 *     buffer and returns a pointer to *that* copy. Copying 38 KB costs well
 *     under a millisecond at 400 MHz and removes the whole class of "the DMA
 *     overwrote the frame while the matcher was reading it" bugs - a torn
 *     frame does not look like a glitch, it looks like a wrong displacement.
 *     A frame whose copy overlaps a DMA wrap is dropped and counted instead.
 * -------------------------------------------------------------------------*/

#ifndef BSP_CAMERA_H_
#define BSP_CAMERA_H_

#include <stdint.h>
#include <stdbool.h>
#include "stm32h7xx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Geometry. 160x120 keeps the 4:3 aspect of the sensor's 400x300 output and
 * costs 38 400 B per frame -> 1.15 MB/s at 30 fps, comfortably inside what
 * DCMI at 12-24 MHz PCLK delivers.
 * -------------------------------------------------------------------------*/
#define CAM_FRAME_W         160u
#define CAM_FRAME_H         120u
#define CAM_BYTES_PER_PX    2u          /* YUV422 */
#define CAM_FRAME_SIZE      (CAM_FRAME_W * CAM_FRAME_H * CAM_BYTES_PER_PX)
#define CAM_FRAME_BUFFERS   2u

#define CAM_I2C_ADDRESS     (0x3Cu << 1)   /* SCCB 0x3C, HAL wants 8-bit */

/* ---------------------------------------------------------------------------
 * Board wiring (inherited from 002.stm32h743_tinyusb_uvc_ov5640, where the same
 * module was verified). Shared with stm32h7xx_hal_msp.c, which owns the pin
 * multiplexing for both I2C4 and DCMI.
 * -------------------------------------------------------------------------*/
#define CAM_PWDN_PORT           GPIOF
#define CAM_PWDN_PIN            GPIO_PIN_13
#define CAM_PWDN_CLK_EN()       __HAL_RCC_GPIOF_CLK_ENABLE()

#define CAM_I2C_INSTANCE        I2C4
#define CAM_I2C_PORT            GPIOF
#define CAM_I2C_SCL_PIN         GPIO_PIN_14
#define CAM_I2C_SDA_PIN         GPIO_PIN_15
#define CAM_I2C_AF              GPIO_AF4_I2C4

/* DCMI 8-bit DVP: HSYNC/VSYNC/PIXCLK + D0..D7 spread over five ports. */
#define CAM_DCMI_GPIO_AF        GPIO_AF13_DCMI

typedef enum
{
    CAM_OK = 0,
    CAM_ERR_I2C,
    CAM_ERR_ID,
    CAM_ERR_SENSOR,
    CAM_ERR_DCMI
} cam_status_t;

typedef struct
{
    uint32_t frames;            /* FRAME interrupts seen                     */
    uint32_t overruns;          /* DCMI FIFO overrun, polled                 */
    uint32_t sync_errors;
    uint32_t restarts;
    uint32_t torn_frames;       /* DMA overtook the consumer                 */
} bsp_camera_stats_t;

/* Power up, probe, configure the sensor and set up DCMI+DMA.
 * Returns CAM_ERR_ID when no sensor answers - callers must treat that as
 * "optical input unavailable" and keep running (the UART path still works). */
cam_status_t bsp_camera_init(void);

uint32_t bsp_camera_get_id(void);

cam_status_t bsp_camera_start(void);
void bsp_camera_stop(void);

/* Main-loop housekeeping: drains the polled OVR/sync flags and restarts the
 * pipeline if the sensor stopped delivering. Cheap enough for every pass. */
void bsp_camera_service(void);

/* Non-blocking. Copies the most recently completed frame into a private work
 * buffer and returns it, or NULL when no new frame has arrived (or the copy
 * raced with the DMA and had to be discarded). The returned pointer stays valid
 * until the next call. */
const uint8_t *bsp_camera_acquire(void);

/* Raw SCCB access - also used by bsp_ov5640_ref.c to push its register table. */
int32_t bsp_camera_read_reg(uint16_t reg, uint8_t *value);
int32_t bsp_camera_write_reg(uint16_t reg, uint8_t value);

void bsp_camera_get_stats(bsp_camera_stats_t *stats);

/* Handles, exported so the interrupt vectors can reach them. */
extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef  hdma_dcmi;
extern I2C_HandleTypeDef  hi2c_cam;

#ifdef __cplusplus
}
#endif

#endif /* BSP_CAMERA_H_ */
