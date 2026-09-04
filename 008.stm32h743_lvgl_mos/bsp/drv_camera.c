/**
  * @file    drv_camera.c
  * @brief   DCMI capture driver: OV5640 320x240 -> centred 192x192 crop ->
  *          DMA2_Stream1 hardware double buffer in the shared sram_pool
  *          (RAM_D2), plus a third snapshot buffer for tear-free preview.
  */
#include "drv_camera.h"
#include "drv_camera_ov5640.h"
#include "bsp_log.h"

#include <string.h>

extern I2C_HandleTypeDef  hi2c4;
extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef  hdma_dcmi;

/* ------------------------------------------------------------------ state */
/* s_frame[0]/[1] are the DMA double-buffer targets; s_frame[2] is the
 * snapshot (present) buffer the LCD blits from.  All three live in RAM_D2
 * and are allocated from sram_pool at open() / freed at close().          */
static uint16_t *s_frame[3];
static void     *s_frame_raw[3];   /* original sram_pool pointers for free */

volatile uint8_t  g_cam_fps;
volatile uint32_t g_cam_frames;
volatile uint32_t g_cam_overruns;
volatile uint8_t  g_cam_last_idx;

static volatile uint8_t s_running;
static volatile uint8_t s_snap_req;    /* set by app, cleared by ISR         */
static volatile uint8_t s_snap_ready;  /* set by ISR after a snapshot copy   */

static void cam_frame_done(uint8_t index);
static void cam_dma_m0_cplt(DMA_HandleTypeDef *hdma);
static void cam_dma_m1_cplt(DMA_HandleTypeDef *hdma);
static void cam_dma_error(DMA_HandleTypeDef *hdma);
static GlobalType_t cam_config_crop(void);

/* -------------------------------------------------------------- allocator */
/* sram_alloc only guarantees 8-byte alignment, but DMA + cache coherency
 * need 32-byte alignment, so we over-allocate and align the payload up. */
#define CAM_ALIGN      32

static uint16_t *cam_alloc_aligned(void **raw_out)
{
    void *raw = sram_alloc(SRAM_REGION_D2, CAM_BYTES + CAM_ALIGN, 4);
    if (raw == NULL)
    {
        return NULL;
    }
    *raw_out = raw;
    return (uint16_t *)(((uintptr_t)raw + (CAM_ALIGN - 1U)) &
                         ~((uintptr_t)CAM_ALIGN - 1U));
}

/* ------------------------------------------------------------------- open */
GlobalType_t drv_camera_open(void)
{
    int i;

    if (drv_camera_ov5640_init() != RT_OK)
    {
        PRINT_LOG("ov5640 init failed\r\n");
        return RT_FAIL;
    }

    if (cam_config_crop() != RT_OK)
    {
        PRINT_LOG("dcmi crop config failed\r\n");
        return RT_FAIL;
    }

    for (i = 0; i < 3; i++)
    {
        s_frame[i] = cam_alloc_aligned(&s_frame_raw[i]);
        if (s_frame[i] == NULL)
        {
            PRINT_LOG("camera buffer alloc failed (%d)\r\n", i);
            /* free what we got so far */
            for (i = i - 1; i >= 0; i--)
            {
                sram_free(SRAM_REGION_D2, s_frame_raw[i]);
                s_frame[i] = NULL;
            }
            return RT_FAIL;
        }
    }

    s_running     = 0;
    s_snap_req    = 0;
    s_snap_ready  = 0;
    g_cam_fps     = 0;
    g_cam_frames  = 0;
    g_cam_overruns = 0;
    g_cam_last_idx = 0;

    PRINT_LOG("camera ready: sensor %dx%d, crop %dx%d\r\n",
           OV5640_WIDTH, OV5640_HEIGHT, CAM_WIDTH, CAM_HEIGHT);
    return RT_OK;
}

void drv_camera_close(void)
{
    int i;

    if (s_running)
    {
        drv_camera_stop();
    }

    for (i = 0; i < 3; i++)
    {
        if (s_frame_raw[i] != NULL)
        {
            sram_free(SRAM_REGION_D2, s_frame_raw[i]);
            s_frame_raw[i] = NULL;
            s_frame[i]     = NULL;
        }
    }
}

/**
 * @brief Centre crop the sensor stream down to CAM_WIDTH x CAM_HEIGHT.
 *
 * Horizontal units are pixel clocks (= bytes for RGB565 over an 8-bit bus),
 * vertical units are lines.  Both size fields are "count - 1".
 */
static GlobalType_t cam_config_crop(void)
{
    uint32_t x0, y0, capcnt, vline;

    if ((CAM_WIDTH > OV5640_WIDTH) || (CAM_HEIGHT > OV5640_HEIGHT))
    {
        return RT_FAIL;
    }

    x0     = (uint32_t)(OV5640_WIDTH - CAM_WIDTH);          /* bytes      */
    capcnt = (uint32_t)(CAM_WIDTH * 2u - 1u);               /* bytes - 1  */

    y0 = (uint32_t)((OV5640_HEIGHT - CAM_HEIGHT) / 2u);
    if (y0 > 0u)
    {
        y0--;                                               /* VST is 0 based */
    }
    vline = (uint32_t)(CAM_HEIGHT - 1u);

    if (HAL_DCMI_ConfigCrop(&hdcmi, x0, y0, capcnt, vline) != HAL_OK)
    {
        return RT_FAIL;
    }
    if (HAL_DCMI_EnableCrop(&hdcmi) != HAL_OK)
    {
        return RT_FAIL;
    }
    return RT_OK;
}

/* ------------------------------------------------------------------ start */
GlobalType_t drv_camera_start(void)
{
    if (s_running)
    {
        return RT_OK;
    }

    s_snap_req   = 0;
    s_snap_ready = 0;

    /* DMA callbacks: in double buffer mode HAL dispatches on the CT bit, so
     * XferCplt == memory 0 finished and XferM1Cplt == memory 1 finished.   */
    hdma_dcmi.XferCpltCallback     = cam_dma_m0_cplt;
    hdma_dcmi.XferM1CpltCallback   = cam_dma_m1_cplt;
    hdma_dcmi.XferHalfCpltCallback = NULL;
    hdma_dcmi.XferM1HalfCpltCallback = NULL;
    hdma_dcmi.XferErrorCallback    = cam_dma_error;

    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_FRAMERI | DCMI_FLAG_OVRRI |
                                  DCMI_FLAG_ERRRI   | DCMI_FLAG_VSYNCRI |
                                  DCMI_FLAG_LINERI);

    /* Continuous grab: CM = 0. */
    hdcmi.Instance->CR &= ~(uint32_t)DCMI_CR_CM;
    __HAL_DCMI_ENABLE(&hdcmi);

    if (HAL_DMAEx_MultiBufferStart_IT(&hdma_dcmi,
                                      (uint32_t)&hdcmi.Instance->DR,
                                      (uint32_t)s_frame[0],
                                      (uint32_t)s_frame[1],
                                      CAM_WORDS) != HAL_OK)
    {
        __HAL_DCMI_DISABLE(&hdcmi);
        return RT_FAIL;
    }

    hdcmi.State = HAL_DCMI_STATE_BUSY;
    __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_OVR | DCMI_IT_ERR);
    hdcmi.Instance->CR |= DCMI_CR_CAPTURE;

    s_running = 1;
    return RT_OK;
}

void drv_camera_stop(void)
{
    if (!s_running)
    {
        return;
    }
    hdcmi.Instance->CR &= ~(uint32_t)DCMI_CR_CAPTURE;
    __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_OVR | DCMI_IT_ERR);
    (void)HAL_DMA_Abort(&hdma_dcmi);
    __HAL_DCMI_DISABLE(&hdcmi);
    hdcmi.State   = HAL_DCMI_STATE_READY;
    s_running     = 0;
    s_snap_req    = 0;
    s_snap_ready  = 0;
}

void drv_camera_recover(void)
{
    drv_camera_stop();
    (void)drv_camera_start();
}

/* ----------------------------------------------------------- snapshot API */
void drv_camera_request_snapshot(void)
{
    s_snap_req = 1;
}

uint8_t drv_camera_snapshot_ready(void)
{
    return s_snap_ready;
}

uint16_t *drv_camera_snapshot_ptr(void)
{
    return s_frame[2];
}

void drv_camera_snapshot_done(void)
{
    s_snap_ready = 0;
}

/* ----------------------------------------------------------------- frames */
static void cam_frame_done(uint8_t index)
{
    static uint32_t tick_ref;
    static uint8_t  counter;

    g_cam_last_idx = index;
    g_cam_frames++;

    /* RAM_D2 is write-back cacheable: drop the stale cache lines for the
     * buffer DMA just filled so the CPU sees the real pixels. */
    SCB_InvalidateDCache_by_Addr((uint32_t *)s_frame[index], (int32_t)CAM_BYTES);

    /* Tear-free handshake: if the app asked for a frame, copy the freshly
     * invalidated buffer into the snapshot buffer and flag it ready.  The
     * copy runs from a buffer DMA is no longer touching, so it is safe. */
    if (s_snap_req)
    {
        memcpy(s_frame[2], s_frame[index], CAM_BYTES);
        s_snap_req   = 0;
        s_snap_ready = 1;
    }

    counter++;
    if ((HAL_GetTick() - tick_ref) >= 1000u)
    {
        tick_ref   = HAL_GetTick();
        g_cam_fps = counter;
        counter    = 0;
    }
}

static void cam_dma_m0_cplt(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    cam_frame_done(0);
}

static void cam_dma_m1_cplt(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    cam_frame_done(1);
}

static void cam_dma_error(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    g_cam_overruns++;
    s_running = 0;          /* the page calls drv_camera_recover() */
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *phdcmi)
{
    (void)phdcmi;
    g_cam_overruns++;
    s_running = 0;
}

/* ------------------------------------------------------------------- SCCB */
GlobalType_t sccb_write_buffer(uint16_t addr, uint8_t *pdata, uint16_t size)
{
    if (HAL_I2C_Mem_Write(&hi2c4, DCMI_DEVICE_ADDRESS, addr, CAM_I2C_MEMADD_SIZE,
                          pdata, size, 1000) != HAL_OK)
    {
        return RT_FAIL;
    }
    return RT_OK;
}

GlobalType_t sccb_write_reg(uint16_t addr, uint8_t data)
{
    if (HAL_I2C_Mem_Write(&hi2c4, DCMI_DEVICE_ADDRESS, addr, CAM_I2C_MEMADD_SIZE,
                          &data, 1, CAM_SCCB_TIMEOUT) != HAL_OK)
    {
        return RT_FAIL;
    }
    return RT_OK;
}

GlobalType_t sccb_read_reg(uint16_t addr, uint8_t *rdata)
{
    if (HAL_I2C_Mem_Read(&hi2c4, DCMI_DEVICE_ADDRESS, addr, CAM_I2C_MEMADD_SIZE,
                         rdata, 1, CAM_SCCB_TIMEOUT) != HAL_OK)
    {
        return RT_FAIL;
    }
    return RT_OK;
}
