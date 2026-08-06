/**
 * @file    drv_dcmi.c
 * @brief   DCMI capture driver: OV5640 320x240 -> centred 192x192 crop ->
 *          DMA2_Stream7 hardware double buffer in non-cacheable AXI SRAM.
 */
#include "drv_dcmi.h"
#include "drv_dcmi_ov5640.h"
#include "logger.h"

extern I2C_HandleTypeDef  hi2c4;
extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef  hdma_dcmi;

/* ------------------------------------------------------------------ state */
/* Two full frames at 0x24000000. The MPU keeps this region non-cacheable so
 * the CPU always sees what DMA wrote without any cache maintenance.        */
DMA_BUFFER static uint16_t s_frame[2][CAPTURE_PIXELS];

volatile uint8_t  g_dcmi_fps;
volatile uint32_t g_dcmi_frames;
volatile uint32_t g_dcmi_overruns;
volatile uint8_t  g_dcmi_last_idx;

static volatile uint8_t s_frame_ready;   /* set by the DMA ISR              */
static volatile uint8_t s_frame_index;   /* buffer that just completed      */
static volatile uint8_t s_running;

static void dcmi_frame_done(uint8_t index);
static void dcmi_dma_m0_cplt(DMA_HandleTypeDef *hdma);
static void dcmi_dma_m1_cplt(DMA_HandleTypeDef *hdma);
static void dcmi_dma_error(DMA_HandleTypeDef *hdma);
static GlobalType_t dcmi_config_crop(void);

/* ------------------------------------------------------------------- init */
GlobalType_t drv_dcmi_init(void)
{
    if (drv_dcmi_ov5640_init() != RT_OK)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "ov5640 init failed");
        return RT_FAIL;
    }

    if (dcmi_config_crop() != RT_OK)
    {
        PRINT_LOG(LOG_ERROR, HAL_GetTick(), "dcmi crop config failed");
        return RT_FAIL;
    }

    PRINT_LOG(LOG_INFO, HAL_GetTick(), "dcmi ready: sensor %dx%d, crop %dx%d",
              OV5640_WIDTH, OV5640_HEIGHT, CAPTURE_WIDTH, CAPTURE_HEIGHT);
    return RT_OK;
}

/**
 * @brief Centre crop the sensor stream down to CAPTURE_WIDTH x CAPTURE_HEIGHT.
 *
 * Horizontal units are pixel clocks (= bytes for RGB565 over an 8-bit bus),
 * vertical units are lines. Both size fields are "count - 1".
 */
static GlobalType_t dcmi_config_crop(void)
{
    uint32_t x0, y0, capcnt, vline;

    if ((CAPTURE_WIDTH > OV5640_WIDTH) || (CAPTURE_HEIGHT > OV5640_HEIGHT))
    {
        return RT_FAIL;
    }

    x0     = (uint32_t)(OV5640_WIDTH - CAPTURE_WIDTH);          /* bytes      */
    capcnt = (uint32_t)(CAPTURE_WIDTH * 2u - 1u);               /* bytes - 1  */

    y0 = (uint32_t)((OV5640_HEIGHT - CAPTURE_HEIGHT) / 2u);
    if (y0 > 0u)
    {
        y0--;                                                   /* VST is 0 based */
    }
    vline = (uint32_t)(CAPTURE_HEIGHT - 1u);

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
GlobalType_t drv_dcmi_start(void)
{
    if (s_running)
    {
        return RT_OK;
    }

    s_frame_ready = 0;
    s_frame_index = 0;

    /* DMA callbacks: in double buffer mode HAL dispatches on the CT bit, so
     * XferCplt == memory 0 finished and XferM1Cplt == memory 1 finished.   */
    hdma_dcmi.XferCpltCallback     = dcmi_dma_m0_cplt;
    hdma_dcmi.XferM1CpltCallback   = dcmi_dma_m1_cplt;
    hdma_dcmi.XferHalfCpltCallback = NULL;
    hdma_dcmi.XferM1HalfCpltCallback = NULL;
    hdma_dcmi.XferErrorCallback    = dcmi_dma_error;

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
                                      CAPTURE_WORDS) != HAL_OK)
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

void drv_dcmi_stop(void)
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
    s_frame_ready = 0;
}

void drv_dcmi_recover(void)
{
    drv_dcmi_stop();
    (void)drv_dcmi_start();
}

/* ----------------------------------------------------------------- frames */
GlobalType_t drv_dcmi_get_frame(uint16_t **frame)
{
    uint8_t idx;

    if (!s_frame_ready)
    {
        return RT_FAIL;
    }

    idx           = s_frame_index;
    s_frame_ready = 0;
    g_dcmi_last_idx = idx;
    *frame        = s_frame[idx];
    return RT_OK;
}

static void dcmi_frame_done(uint8_t index)
{
    static uint32_t tick_ref;
    static uint8_t  counter;

    s_frame_index = index;
    s_frame_ready = 1;
    g_dcmi_frames++;

    counter++;
    if ((HAL_GetTick() - tick_ref) >= 1000u)
    {
        tick_ref   = HAL_GetTick();
        g_dcmi_fps = counter;
        counter    = 0;
    }
}

static void dcmi_dma_m0_cplt(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    dcmi_frame_done(0);
}

static void dcmi_dma_m1_cplt(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    dcmi_frame_done(1);
}

static void dcmi_dma_error(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    g_dcmi_overruns++;
    s_running = 0;          /* the application calls drv_dcmi_recover() */
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *phdcmi)
{
    (void)phdcmi;
    g_dcmi_overruns++;
    s_running = 0;
}

/* ------------------------------------------------------------------- SCCB */
GlobalType_t sccb_write_buffer(uint16_t addr, uint8_t *pdata, uint16_t size)
{
    if (HAL_I2C_Mem_Write(&hi2c4, DCMI_DEVICE_ADDRESS, addr, I2C_MEMADD_SIZE,
                          pdata, size, 1000) != HAL_OK)
    {
        return RT_FAIL;
    }
    return RT_OK;
}

GlobalType_t sccb_write_reg(uint16_t addr, uint8_t data)
{
    if (HAL_I2C_Mem_Write(&hi2c4, DCMI_DEVICE_ADDRESS, addr, I2C_MEMADD_SIZE,
                          &data, 1, DCMI_TIMEOUT) != HAL_OK)
    {
        return RT_FAIL;
    }
    return RT_OK;
}

GlobalType_t sccb_read_reg(uint16_t addr, uint8_t *rdata)
{
    if (HAL_I2C_Mem_Read(&hi2c4, DCMI_DEVICE_ADDRESS, addr, I2C_MEMADD_SIZE,
                         rdata, 1, DCMI_TIMEOUT) != HAL_OK)
    {
        return RT_FAIL;
    }
    return RT_OK;
}
