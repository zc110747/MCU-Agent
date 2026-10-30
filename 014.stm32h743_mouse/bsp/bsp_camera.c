/* ---------------------------------------------------------------------------
 * bsp_camera.c - OV5640 over SCCB, DCMI over DMA2, two frame buffers.
 * See bsp_camera.h for the design rationale.
 * -------------------------------------------------------------------------*/

#include <string.h>
#include "bsp_camera.h"
#include "bsp.h"
#include "ov5640.h"
#include "bsp_ov5640_ref.h"

/* Chip ID reported by a healthy OV5640 (registers 0x300A/0x300B). */
#define OV5640_CHIP_ID          0x5640u

/* SCCB timing: 100 kHz at the 100 MHz APB4 kernel clock. */
#define CAM_I2C_TIMING          0xF0421E25u

/* Sensor-side output; the DCMI crop carves the frame out of it. Keeping the
 * sensor at 4:3 preserves the ISP window the register table was tuned for. */
#define CAM_SENSOR_W            400u
#define CAM_SENSOR_H            300u

#define CAM_IRQ_PRIO            5u
#define CAM_WATCHDOG_MS         500u

DCMI_HandleTypeDef hdcmi;
DMA_HandleTypeDef  hdma_dcmi;
I2C_HandleTypeDef  hi2c_cam;

static OV5640_Object_t s_ov5640;
static uint32_t        s_ov5640_id;

/* Two frames, circular DMA. NOLOAD + 32-byte aligned: see the linker script,
 * and "DMA buffer placement" in bsp.h for the pragma pair. */
BSP_DMA_BUF_BEGIN(".cam_buf")
static uint8_t s_frame[CAM_FRAME_BUFFERS][CAM_FRAME_SIZE] BSP_DMA_BUF_ATTR(".cam_buf");
BSP_DMA_BUF_END()

/* Frame hand-off. s_ready_half == 0xFF means "nothing completed yet". */
static volatile uint8_t  s_ready_half = 0xFFu;
static volatile bool     s_frame_pending;
static bool              s_streaming;

/* Private work buffer the completed frame is copied into before the matcher
 * sees it - see the busy-state contract in bsp_camera.h. CPU-only, so DTCM. */
static uint8_t s_frame_work[CAM_FRAME_SIZE] __attribute__((aligned(32)));

static bsp_camera_stats_t s_stats;
static uint32_t s_last_frame_ms;

/* ---------------------------------------------------------------------------
 * SCCB glue for the ST component driver
 * -------------------------------------------------------------------------*/
static int32_t cam_i2c_init(void)
{
    hi2c_cam.Instance              = CAM_I2C_INSTANCE;
    hi2c_cam.Init.Timing           = CAM_I2C_TIMING;
    hi2c_cam.Init.OwnAddress1      = 0;
    hi2c_cam.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c_cam.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c_cam.Init.OwnAddress2      = 0;
    hi2c_cam.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c_cam.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c_cam.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c_cam) != HAL_OK)
    {
        return -1;
    }
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c_cam, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
    {
        return -1;
    }
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c_cam, 0) != HAL_OK)
    {
        return -1;
    }

    return 0;
}

static int32_t cam_i2c_deinit(void)
{
    return (HAL_I2C_DeInit(&hi2c_cam) == HAL_OK) ? 0 : -1;
}

static int32_t cam_i2c_write(uint16_t address, uint16_t reg, uint8_t *data, uint16_t len)
{
    /* 1000 ms timeout: SCCB clock stretching on a marginal cable is common on
     * this sensor, and a shorter timeout turns it into a spurious CAM_ERR_I2C
     * during power-up. */
    return (HAL_I2C_Mem_Write(&hi2c_cam, address, reg, I2C_MEMADD_SIZE_16BIT,
                              data, len, 1000) == HAL_OK) ? 0 : -1;
}

static int32_t cam_i2c_read(uint16_t address, uint16_t reg, uint8_t *data, uint16_t len)
{
    return (HAL_I2C_Mem_Read(&hi2c_cam, address, reg, I2C_MEMADD_SIZE_16BIT,
                             data, len, 1000) == HAL_OK) ? 0 : -1;
}

static int32_t cam_get_tick(void)
{
    return (int32_t) HAL_GetTick();
}

int32_t bsp_camera_read_reg(uint16_t reg, uint8_t *value)
{
    return cam_i2c_read(CAM_I2C_ADDRESS, reg, value, 1u);
}

int32_t bsp_camera_write_reg(uint16_t reg, uint8_t value)
{
    return cam_i2c_write(CAM_I2C_ADDRESS, reg, &value, 1u);
}

/* ---------------------------------------------------------------------------
 * Power
 * -------------------------------------------------------------------------*/
static void cam_power_pin_init(void)
{
    GPIO_InitTypeDef gpio = {0u};

    CAM_PWDN_CLK_EN();

    gpio.Pin   = CAM_PWDN_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CAM_PWDN_PORT, &gpio);
}

static void cam_power_up(void)
{
    /* PWDN is active high on this module. Assert briefly, release, then give
     * the sensor's internal regulator time to settle before the first SCCB
     * access - 50 ms is what the vendor sequence and 002 both use. */
    HAL_GPIO_WritePin(CAM_PWDN_PORT, CAM_PWDN_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(CAM_PWDN_PORT, CAM_PWDN_PIN, GPIO_PIN_RESET);
    HAL_Delay(50);
}

/* ---------------------------------------------------------------------------
 * DCMI
 * -------------------------------------------------------------------------*/
static cam_status_t cam_dcmi_init(void)
{
    hdcmi.Instance              = DCMI;
    hdcmi.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
    hdcmi.Init.PCKPolarity      = DCMI_PCKPOLARITY_RISING;
    hdcmi.Init.VSPolarity       = DCMI_VSPOLARITY_LOW;
    hdcmi.Init.HSPolarity       = DCMI_HSPOLARITY_LOW;
    hdcmi.Init.CaptureRate      = DCMI_CR_ALL_FRAME;
    hdcmi.Init.ExtendedDataMode = DCMI_EXTEND_DATA_8B;
    hdcmi.Init.JPEGMode         = DCMI_JPEG_DISABLE;
    hdcmi.Init.ByteSelectMode   = DCMI_BSM_ALL;
    hdcmi.Init.ByteSelectStart  = DCMI_OEBS_ODD;
    hdcmi.Init.LineSelectMode   = DCMI_LSM_ALL;
    hdcmi.Init.LineSelectStart  = DCMI_OELS_ODD;

    if (HAL_DCMI_Init(&hdcmi) != HAL_OK)
    {
        return CAM_ERR_DCMI;
    }

    /* HAL_DCMI_Init() unconditionally enables LINE | VSYNC | ERR | OVR. ERR and
     * OVR are the destructive pair: HAL_DCMI_IRQHandler() answers them by
     * aborting the DMA and latching HAL_DCMI_STATE_ERROR, so one transient
     * glitch would stop capture permanently. They are polled in
     * bsp_camera_service() instead. FRAME stays on - it is the frame boundary
     * the consumer keys on. */
    __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_LINE | DCMI_IT_VSYNC |
                                  DCMI_IT_ERR  | DCMI_IT_OVR);

    /* Crop window in sensor pixel coordinates. The sensor emits 400x300 YUYV;
     * the DCMI shifts in 2 bytes per pixel, so X is expressed in bytes. */
    {
        const uint32_t x0    = ((CAM_SENSOR_W - CAM_FRAME_W) / 2u) * CAM_BYTES_PER_PX;
        const uint32_t y0    = (CAM_SENSOR_H - CAM_FRAME_H) / 2u;
        const uint32_t xsize = (CAM_FRAME_W * CAM_BYTES_PER_PX) - 1u;
        const uint32_t ysize = CAM_FRAME_H - 1u;

        if (HAL_DCMI_ConfigCrop(&hdcmi, x0, y0, xsize, ysize) != HAL_OK)
        {
            return CAM_ERR_DCMI;
        }
        if (HAL_DCMI_EnableCrop(&hdcmi) != HAL_OK)
        {
            return CAM_ERR_DCMI;
        }
    }

    return CAM_OK;
}

/* ---------------------------------------------------------------------------
 * Interrupts
 * -------------------------------------------------------------------------*/
void DCMI_IRQHandler(void)
{
    HAL_DCMI_IRQHandler(&hdcmi);
}

void DMA2_Stream1_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_dcmi);
}

void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *hdcmi_handle)
{
    uint32_t ndtr;
    uint32_t written;
    uint8_t  current_half;

    (void) hdcmi_handle;

    ndtr = ((DMA_Stream_TypeDef *) hdma_dcmi.Instance)->NDTR;
    if (ndtr > (CAM_FRAME_BUFFERS * CAM_FRAME_SIZE))
    {
        ndtr = CAM_FRAME_BUFFERS * CAM_FRAME_SIZE;
    }

    /* How much of the two-frame ring the DMA has written. Whichever half it is
     * currently filling, the *other* half is the one that just completed. */
    written      = (CAM_FRAME_BUFFERS * CAM_FRAME_SIZE) - ndtr;
    current_half = (written >= CAM_FRAME_SIZE) ? 1u : 0u;

    s_ready_half    = (uint8_t) (current_half ^ 1u);
    s_frame_pending = true;
    s_stats.frames++;
    s_last_frame_ms = board_millis();
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
cam_status_t bsp_camera_init(void)
{
    OV5640_IO_t io;

    s_stats.frames       = 0u;
    s_stats.overruns     = 0u;
    s_stats.sync_errors  = 0u;
    s_stats.restarts     = 0u;
    s_stats.torn_frames  = 0u;

    s_ready_half    = 0xFFu;
    s_frame_pending = false;
    s_streaming     = false;

    cam_power_pin_init();
    cam_power_up();

    if (cam_i2c_init() != 0)
    {
        return CAM_ERR_I2C;
    }

    io.Init     = cam_i2c_init;
    io.DeInit   = cam_i2c_deinit;
    io.Address  = CAM_I2C_ADDRESS;
    io.WriteReg = cam_i2c_write;
    io.ReadReg  = cam_i2c_read;
    io.GetTick  = cam_get_tick;

    if (OV5640_RegisterBusIO(&s_ov5640, &io) != OV5640_OK)
    {
        return CAM_ERR_I2C;
    }
    if (OV5640_ReadID(&s_ov5640, &s_ov5640_id) != OV5640_OK)
    {
        return CAM_ERR_I2C;
    }
    if (s_ov5640_id != OV5640_CHIP_ID)
    {
        return CAM_ERR_ID;
    }

    /* The vendor table (not the ST BSP table - see bsp_ov5640_ref.c) leaves the
     * sensor streaming: 0x3008 = 0x02 is its last write. */
    if (ov5640_ref_init() != 0)
    {
        return CAM_ERR_SENSOR;
    }

    return cam_dcmi_init();
}

uint32_t bsp_camera_get_id(void)
{
    return s_ov5640_id;
}

cam_status_t bsp_camera_start(void)
{
    uint32_t words = (CAM_FRAME_BUFFERS * CAM_FRAME_SIZE) / 4u;

    s_ready_half    = 0xFFu;
    s_frame_pending = false;

    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                           (uint32_t) s_frame[0], words) != HAL_OK)
    {
        return CAM_ERR_DCMI;
    }

    s_streaming     = true;
    s_last_frame_ms = board_millis();
    return CAM_OK;
}

void bsp_camera_stop(void)
{
    if (s_streaming == false)
    {
        return;
    }

    (void) HAL_DCMI_Stop(&hdcmi);
    s_streaming = false;
}

const uint8_t *bsp_camera_acquire(void)
{
    uint8_t  half;
    uint32_t ring = CAM_FRAME_BUFFERS * CAM_FRAME_SIZE;
    uint32_t ndtr_before;
    uint32_t ndtr_after;
    uint32_t elapsed;

    if ((s_frame_pending == false) || (s_ready_half > 1u))
    {
        return 0;
    }

    s_frame_pending = false;
    half = s_ready_half;

    ndtr_before = ((DMA_Stream_TypeDef *) hdma_dcmi.Instance)->NDTR;
    if (ndtr_before > ring)
    {
        ndtr_before = ring;
    }

    /* The DMA wrote this half behind the D-cache. Drop the stale lines before
     * the CPU looks at them. The region is 32-byte aligned and a multiple of
     * 32 bytes, so no neighbouring object's line is touched. */
    SCB_InvalidateDCache_by_Addr((uint32_t *) s_frame[half], (int32_t) CAM_FRAME_SIZE);
    memcpy(s_frame_work, s_frame[half], CAM_FRAME_SIZE);

    ndtr_after = ((DMA_Stream_TypeDef *) hdma_dcmi.Instance)->NDTR;
    if (ndtr_after > ring)
    {
        ndtr_after = ring;
    }

    /* NDTR counts down. If the DMA advanced by more than half the ring while
     * the copy was running it has already started refilling the buffer we just
     * read, so the copy may straddle two frames. Drop it rather than hand the
     * matcher a seam. */
    elapsed = (ndtr_before + ring - ndtr_after) % ring;
    if (elapsed > (CAM_FRAME_SIZE / 2u))
    {
        s_stats.torn_frames++;
        return 0;
    }

    return s_frame_work;
}

void bsp_camera_service(void)
{
    if (s_streaming == false)
    {
        return;
    }

    /* Polled error handling: clear the flags so the peripheral does not latch
     * them, count them, and carry on. Nothing is aborted. */
    if ((DCMI->RISR & DCMI_FLAG_OVRRI) != 0u)
    {
        __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI);
        s_stats.overruns++;
    }
    if ((DCMI->RISR & DCMI_FLAG_ERRRI) != 0u)
    {
        __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_ERRRI);
        s_stats.sync_errors++;
    }

    /* Watchdog. The sensor free-runs at >= 8 fps, so 500 ms of silence means
     * the pipeline wedged (cable, power, sensor reset) rather than jitter. */
    if ((board_millis() - s_last_frame_ms) > CAM_WATCHDOG_MS)
    {
        (void) HAL_DCMI_Stop(&hdcmi);
        s_streaming = false;
        s_stats.restarts++;
        (void) bsp_camera_start();
    }
}

void bsp_camera_get_stats(bsp_camera_stats_t *stats)
{
    if (stats == 0)
    {
        return;
    }

    *stats = s_stats;
}
