/**
 ******************************************************************************
 * @file    bsp_camera.c
 * @brief   OV5640 camera front-end - CONTINUOUS mode with ping-pong buffers.
 *
 * Data path:
 *   OV5640 --(8 bit DVP, YUV422/YUYV, 320x240)--> DCMI --crop 240x240-->
 *   DMA2_Stream1 (double-buffer, circular) --> AXI SRAM frame buffer (NC)
 *
 * Why CONTINUOUS + CIRCULAR, and why ERR/OVR interrupts stay OFF
 * --------------------------------------------------------------
 * SNAPSHOT mode disables the DCMI after every frame. Restarting it means
 * re-synchronising on the next VSYNC edge, and Start_DMA() cannot be called
 * from inside the FRAME callback (the HAL lock is still held), so frames are
 * lost or the pipeline stalls outright.
 *
 * CONTINUOUS mode keeps CAPTURE asserted forever. That only works if the DMA
 * is CIRCULAR: a NORMAL-mode stream switches itself off after the first frame
 * while the DCMI keeps shifting pixels in, which overruns the DCMI FIFO on
 * frame 2.
 *
 * The overrun is what used to kill us. HAL_DCMI_IRQHandler() reacts to OVR
 * (and to a sync ERR) by calling HAL_DMA_Abort_IT() and parking the handle in
 * HAL_DCMI_STATE_ERROR - a single glitch permanently stops capture. Since the
 * handler tests MISR (masked status), simply never enabling DCMI_IT_OVR /
 * DCMI_IT_ERR keeps it out of that destructive path. We still observe both
 * conditions by polling RISR from bsp_camera_service(), where a glitch costs
 * one frame instead of the whole stream.
 ******************************************************************************
 */

#include "bsp_camera.h"
#include "sys_prob.h"
#include "ov5640.h"
#include "bsp_ov5640_ref.h"
#include <string.h>

/* Chip ID reported by a healthy OV5640 (registers 0x300A/0x300B). */
#define OV5640_CHIP_ID 0x5640U

DCMI_HandleTypeDef     hdcmi;
DMA_HandleTypeDef      hdma_dcmi;
I2C_HandleTypeDef      hi2c_cam;
static OV5640_Object_t ov5640_obj;
static uint32_t        ov5640_id = 0;

/* Camera diagnostic telemetry. Every field below used to be a loose file-scope
 * global named cam_*. They are grouped by sub-system and are file-static: none
 * are referenced from another translation unit, so they are reachable only
 * over SWD as s_cam_diag.<group>.<field>. */
typedef struct
{
    volatile uint32_t frame_count;
    volatile uint32_t error_count;
    volatile uint32_t start_count;
    volatile uint32_t start_fail_count;
    volatile uint32_t ovr_count;
    volatile uint32_t sync_err_count;
    volatile uint32_t restart_count;
    volatile uint32_t last_frame_ms;
} cam_stats_t;

typedef struct
{
    volatile uint32_t test_pattern;
    volatile uint32_t pclk_pol;
    volatile uint32_t crop_en;
    volatile uint32_t words_per_frame;
    volatile uint32_t flicker;
    volatile uint32_t flicker_reg;
    volatile uint32_t flicker_a;
    volatile uint32_t flicker_b;
    volatile uint32_t force_run;
} cam_control_t;

typedef struct
{
    volatile uint32_t snap_test;
    volatile uint32_t snap_unsync;
    volatile uint32_t unsync_ndtr;
    volatile uint32_t snap_ok;
    volatile uint32_t snap_wait;
    volatile uint32_t snap_torn;
    volatile uint32_t snap_ndtr0;
    volatile uint32_t snap_ndtr1;
    volatile uint32_t snap_cycles;
} cam_snap_t;

typedef struct
{
    cam_stats_t   stats;
    cam_control_t ctl;
    cam_snap_t    snap;
} cam_diag_t;

/* Defaults MUST mirror the sensor wiring - they are not arbitrary knobs:
 *   pclk_pol = 1  sample PIXCLK on the rising edge. The sensor runs
 *                 0x4740 = 0x21 (PCLK inverted, VSYNC active low), which pairs
 *                 with DCMI_PCKPOLARITY_RISING. Sampling on the wrong edge
 *                 makes the DCMI mis-read HSYNC/VSYNC, so it never sees the
 *                 vertical blanking and the DMA never parks - every frame
 *                 snapshot is then torn and nothing reaches USB.
 *   crop_en = 1   keep the DCMI crop window enabled. With it off the DCMI takes
 *                 the sensor's full 400-pixel line (240000 B/frame) while the
 *                 DMA is sized for a 115200 B frame, so the circular wrap no
 *                 longer lands in blanking either. */
static cam_diag_t s_cam_diag = {
    .ctl.test_pattern     = 0U,
    .ctl.pclk_pol         = 1U,
    .ctl.crop_en          = 1U,
    .ctl.force_run        = 0U,
    .ctl.flicker_reg      = 0x5580U,
    .ctl.flicker_a        = 0x06U,
    .ctl.flicker_b        = 0x46U,
};

/* Camera capture runtime state. All fields are one logical "thing" (the DCMI /
 * DMA pipeline bookkeeping) and are initialised / reset together. The three
 * applied_* fields keep a 0xFFFFFFFFU sentinel until the debugger first writes
 * the corresponding cam_* control variable. */
typedef struct
{
    volatile bool     frame_done;       /* set by DCMI FrameEventCallback */
    uint8_t          *fb_base;          /* set by bsp_camera_set_buffers() */
    volatile bool     auto_running;     /* capture is meant to be running */
    uint32_t          flicker_fc;       /* frame count at last flicker flip */
    bool              flicker_on;       /* current negative-image polarity */
    uint32_t          applied_pattern;  /* last s_cam_diag.ctl.test_pattern written */
    uint32_t          applied_pclk_pol; /* last s_cam_diag.ctl.pclk_pol applied to CR */
    uint32_t          applied_crop;     /* last s_cam_diag.ctl.crop_en applied */
    volatile uint32_t prev_ndtr;        /* NDTR sampled at previous FRAME IRQ */
} cam_state_t;

static cam_state_t s_cam = {
    .applied_pattern  = 0xFFFFFFFFU,
    .applied_pclk_pol = 0xFFFFFFFFU,
    .applied_crop     = 0xFFFFFFFFU,
};

/* Poke from the debugger to drive the sensor's internal colour-bar generator:
 *   0 = normal imaging, 1 = colour bars (OV5640 reg 0x503D = 0x80).
 * A clean bar pattern proves the DCMI/DMA path and puts the blame on the
 * sensor's imaging config; stripes in bar mode blame the DVP wiring. */
/* DCMI sampling edge, switchable from the debugger without a reflash:
 *   1 = sample on PIXCLK rising edge, 0 = falling edge.
 *
 * The sensor is programmed with 0x4740 = 0x21 (PCLK inverted, VSYNC active
 * low), which pairs with DCMI_PCKPOLARITY_RISING. This is the combination used
 * by the reference design that runs on this exact sensor and MCU.
 *
 * Historical note: the "one constant byte per row" symptom we chased here was
 * never a sampling-edge problem - it was 0x3814 = 0x31 with horizontal binning
 * left disabled. Both edges produced constant data because the bus itself was
 * static. See ov5640_ref.c. */
/* Crop on/off at runtime. With crop disabled the DCMI takes the sensor's full
 * 320x240 line instead of the centred 240-pixel window, which tells us whether
 * the crop window itself is landing somewhere useless. */
/* Words actually delivered between two FRAME interrupts, derived from the
 * circular DMA counter. A correct 240x240 YUY2 capture must show exactly
 * 28800 words (115200 bytes); anything else means the DCMI is taking in a
 * different amount of data than the crop window claims. */
/* Force continuous capture to run even with no USB host, so the frame buffer
 * can be inspected over SWD. Set s_cam_diag.ctl.force_run = 1 from the debugger. The
 * DCMI keeps filling s_cam.fb_base; uvc_app's own start/stop is unaffected while
 * the host is silent. */
/* The DMA runs circular over a single frame buffer, so there is no ownership
 * hand-off to track: the consumer always reads s_cam.fb_base and simply learns
 * from s_cam.frame_done that a fresh VSYNC boundary went by. */

/* s_cam_diag.ctl.flicker bookkeeping: which frame we last flipped on, and the current
 * polarity of the negative-image effect. */

/* ==========================================================================
 * SCCB (I2C4) bus glue for the ST OV5640 component driver
 * ========================================================================== */

static int32_t cam_i2c_init(void)
{
    hi2c_cam.Instance              = CAM_I2C_INSTANCE;
    hi2c_cam.Init.Timing           = 0xF0421E25U;
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

static int32_t cam_i2c_write(uint16_t addr, uint16_t reg, uint8_t *pdata, uint16_t len)
{
    return (HAL_I2C_Mem_Write(&hi2c_cam, addr, reg, I2C_MEMADD_SIZE_16BIT,
                              pdata, len, 1000) == HAL_OK)
               ? 0
               : -1;
}

static int32_t cam_i2c_read(uint16_t addr, uint16_t reg, uint8_t *pdata, uint16_t len)
{
    return (HAL_I2C_Mem_Read(&hi2c_cam, addr, reg, I2C_MEMADD_SIZE_16BIT,
                             pdata, len, 1000) == HAL_OK)
               ? 0
               : -1;
}

static int32_t cam_get_tick(void)
{
    return (int32_t)HAL_GetTick();
}

/* ==========================================================================
 * Power / reset control
 * ========================================================================== */
static void cam_power_pin_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    CAM_PWDN_CLK_ENABLE();

    gpio.Pin   = CAM_PWDN_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CAM_PWDN_PORT, &gpio);
}

static void cam_power_up(void)
{
    HAL_GPIO_WritePin(CAM_PWDN_PORT, CAM_PWDN_PIN, GPIO_PIN_SET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(CAM_PWDN_PORT, CAM_PWDN_PIN, GPIO_PIN_RESET);
    HAL_Delay(50);
}

/* ==========================================================================
 * DCMI + DMA (continuous, double-buffer)
 * ========================================================================== */
static cam_status_t cam_dcmi_init(void)
{
    hdcmi.Instance              = DCMI;
    hdcmi.Init.SynchroMode      = DCMI_SYNCHRO_HARDWARE;
    hdcmi.Init.PCKPolarity      = (s_cam_diag.ctl.pclk_pol != 0U) ? DCMI_PCKPOLARITY_RISING
                                                                  : DCMI_PCKPOLARITY_FALLING;
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

    /* HAL_DCMI_Init() unconditionally enables LINE | VSYNC | ERR | OVR (see
     * stm32h7xx_hal_dcmi.c:256). We must undo that:
     *
     *   OVR / ERR - HAL_DCMI_IRQHandler() answers these by aborting the DMA and
     *               latching HAL_DCMI_STATE_ERROR, so one transient glitch stops
     *               capture for good. In SNAPSHOT mode the frame handler happens
     *               to mask them again after frame 1; in CONTINUOUS mode nothing
     *               ever does, which is what pinned us at exactly one frame.
     *               bsp_camera_service() polls RISR for these instead.
     *   LINE      - fires once per scan line: 240 needless IRQs per frame.
     *   VSYNC     - redundant, FRAME already marks the boundary.
     *
     * FRAME stays under HAL control: it is switched on by DCMI_DMAXferCplt(). */
    __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_LINE | DCMI_IT_VSYNC |
                                      DCMI_IT_ERR | DCMI_IT_OVR);

    const uint32_t x0    = ((CAM_SENSOR_WIDTH - FRAME_WIDTH) / 2U) * FRAME_BYTES_PER_PX;
    const uint32_t y0    = (CAM_SENSOR_HEIGHT - FRAME_HEIGHT) / 2U;
    const uint32_t xsize = (FRAME_WIDTH * FRAME_BYTES_PER_PX) - 1U;
    const uint32_t ysize = FRAME_HEIGHT - 1U;

    if (HAL_DCMI_ConfigCrop(&hdcmi, x0, y0, xsize, ysize) != HAL_OK)
    {
        return CAM_ERR_DCMI;
    }
    if (HAL_DCMI_EnableCrop(&hdcmi) != HAL_OK)
    {
        return CAM_ERR_DCMI;
    }
    return CAM_OK;
}

/* ==========================================================================
 * Public API
 * ========================================================================== */
cam_status_t bsp_camera_init(void)
{
    OV5640_IO_t io;

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

    if (OV5640_RegisterBusIO(&ov5640_obj, &io) != OV5640_OK)
    {
        return CAM_ERR_I2C;
    }
    if (OV5640_ReadID(&ov5640_obj, &ov5640_id) != OV5640_OK)
    {
        return CAM_ERR_I2C;
    }
    if (ov5640_id != OV5640_CHIP_ID)
    {
        return CAM_ERR_ID;
    }
    /* The ST BSP tables are not used: OV5640_R320x240 programs a horizontal
     * sub-sample increment without enabling horizontal binning, which freezes
     * the column address generator and repeats one pixel across every line.
     * ov5640_ref_init() applies a sequence verified on this hardware and leaves
     * the sensor streaming (0x3008 = 0x02 is the last write). */
    if (ov5640_ref_init() != 0)
    {
        return CAM_ERR_SENSOR;
    }
    if (cam_dcmi_init() != CAM_OK)
    {
        return CAM_ERR_DCMI;
    }

#ifdef CAM_DIAGNOSTICS
    sys_prob_snapshot_regs();
#endif

    return CAM_OK;
}

/* Raw SCCB access - the ST component driver keeps its register helpers
 * private, and we need arbitrary addresses for diagnostics. */
int32_t bsp_camera_read_reg(uint16_t reg, uint8_t *val)
{
    return cam_i2c_read(CAM_I2C_ADDRESS, reg, val, 1);
}

int32_t bsp_camera_write_reg(uint16_t reg, uint8_t val)
{
    return cam_i2c_write(CAM_I2C_ADDRESS, reg, &val, 1);
}

/* Pack the 11 DVP signals, scattered over five ports, into one word. */

/* Flip the DVP pins between DCMI alternate function and plain floating input. */
void bsp_camera_dvp_pins_mode(bool as_input)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Mode      = as_input ? GPIO_MODE_INPUT : GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF13_DCMI;

    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_6; /* HSYNC, PIXCLK */
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7; /* D0, D1 */
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_3; /* D5 */
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6; /* D4, D6, D7 */
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11; /* VSYNC, D2, D3 */
    HAL_GPIO_Init(GPIOG, &gpio);
}

/* Re-init the eleven DVP lines as inputs with the requested pull setting. */
void bsp_camera_dvp_pins_pull(uint32_t pull)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = pull;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_6; /* HSYNC, PIXCLK */
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7; /* D0, D1 */
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_3; /* D5 */
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6; /* D4, D6, D7 */
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11; /* VSYNC, D2, D3 */
    HAL_GPIO_Init(GPIOG, &gpio);
}

uint32_t bsp_camera_get_id(void)
{
    return ov5640_id;
}

/* Start continuous capture into buf[0].
 *
 * buf[0] must be 32-byte aligned and at least FRAME_SIZE bytes. After this
 * call the DCMI+DMA pair runs autonomously; the caller pulls coherent frames
 * out with bsp_camera_snapshot(). */
cam_status_t bsp_camera_start_continuous(uint8_t (*buf)[FRAME_SIZE])
{
    /* Clear any stale error latched while the pipeline was idle, otherwise the
     * first service() poll would immediately report a bogus overrun. */
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);

    /* CONTINUOUS + circular DMA: hardware free-runs, the CPU is never in the
     * capture path. Deliberately no DCMI_IT_ERR / DCMI_IT_OVR here - see the
     * file header for why enabling them is fatal. */
    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                           (uint32_t)buf[0], FRAME_SIZE / 4U) != HAL_OK)
    {
        s_cam_diag.stats.start_fail_count++;
        s_cam.auto_running = false;
        return CAM_ERR_DCMI;
    }

    s_cam.auto_running             = true;
    s_cam.frame_done               = false;
    s_cam_diag.stats.last_frame_ms = HAL_GetTick();
    s_cam_diag.stats.start_count++;
    return CAM_OK;
}

void bsp_camera_stop(void)
{
    s_cam.auto_running = false;
    HAL_DCMI_Stop(&hdcmi);
    s_cam.frame_done = false;
}

/* Main-loop housekeeping. Must be called regularly while streaming.
 *
 *  1. Drains the OVR / sync-error flags that we deliberately left un-masked
 *     at NVIC level, so they are recorded but never abort the DMA.
 *  2. Watchdog: a wedged sensor (or a genuinely aborted DMA) shows up as
 *     "no FRAME interrupt for CAM_WATCHDOG_MS". Rebuild the pipeline instead
 *     of streaming a frozen image forever. */
bool bsp_camera_is_auto_running(void)
{
    return s_cam.auto_running;
}

void bsp_camera_restart_continuous(void)
{
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                           (uint32_t)s_cam.fb_base, FRAME_SIZE / 4U) == HAL_OK)
    {
        s_cam.auto_running             = true;
        s_cam_diag.stats.last_frame_ms = HAL_GetTick();
        s_cam_diag.stats.start_count++;
    }
}

void bsp_camera_service(void)
{
#ifdef CAM_DIAGNOSTICS
    sys_prob_service();
#endif
    /* Debug: drive the snapshot path with no USB host attached, so the tear
     * telemetry and the resulting buffer can both be inspected over SWD.
     * Destination is the first transmit-side buffer, s_cam.fb_base + FRAME_SIZE. */
    if ((s_cam_diag.snap.snap_test != 0U) && s_cam.auto_running && (s_cam.fb_base != NULL))
    {
        (void)bsp_camera_snapshot(s_cam.fb_base + FRAME_SIZE);
    }

    /* Debug A/B control: one unsynchronised copy into the *second* transmit
     * buffer, taken at whatever phase the DMA happens to be at. This is what
     * the code used to do on every frame, so dumping both buffers from one run
     * shows the fix and the bug side by side. */
    if ((s_cam_diag.snap.snap_unsync != 0U) && s_cam.auto_running && (s_cam.fb_base != NULL))
    {
        s_cam_diag.snap.snap_unsync = 0;
        s_cam_diag.snap.unsync_ndtr = __HAL_DMA_GET_COUNTER(&hdma_dcmi);
        memcpy(s_cam.fb_base + 2U * FRAME_SIZE, s_cam.fb_base, FRAME_SIZE);
    }

    /* Debug: flip the sensor between normal and negative on every frame. No
     * real scene can change more violently than that, so a snapshot that spans
     * two frames stops being a subtle seam and becomes a full-scale luma step
     * at the splice - trivially measurable from a single dumped frame. */
    if (s_cam_diag.ctl.flicker != 0U)
    {
        if (s_cam_diag.stats.frame_count != s_cam.flicker_fc)
        {
            s_cam.flicker_fc = s_cam_diag.stats.frame_count;
            s_cam.flicker_on = !s_cam.flicker_on;
            (void)bsp_camera_write_reg((uint16_t)s_cam_diag.ctl.flicker_reg,
                                       (uint8_t)(s_cam.flicker_on ? s_cam_diag.ctl.flicker_b
                                                                  : s_cam_diag.ctl.flicker_a));
        }
    }

    if (s_cam_diag.ctl.crop_en != s_cam.applied_crop)
    {
        s_cam.applied_crop     = s_cam_diag.ctl.crop_en;
        const bool was_running = s_cam.auto_running;

        if (was_running)
        {
            HAL_DCMI_Stop(&hdcmi);
        }
        if (s_cam_diag.ctl.crop_en != 0U)
        {
            (void)HAL_DCMI_EnableCrop(&hdcmi);
        }
        else
        {
            (void)HAL_DCMI_DisableCrop(&hdcmi);
        }
        if (was_running)
        {
            __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
            if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                                   (uint32_t)s_cam.fb_base, FRAME_SIZE / 4U) == HAL_OK)
            {
                s_cam_diag.stats.start_count++;
            }
            else
            {
                s_cam_diag.stats.start_fail_count++;
                s_cam.auto_running = false;
            }
            s_cam_diag.stats.last_frame_ms = HAL_GetTick();
        }
    }

    /* Debugger-driven test-pattern toggle. Handled before the running check so
     * it can be armed while the pipeline is still idle. */
    if (s_cam_diag.ctl.test_pattern != s_cam.applied_pattern)
    {
        s_cam.applied_pattern = s_cam_diag.ctl.test_pattern;
        (void)bsp_camera_write_reg(0x503D, (s_cam_diag.ctl.test_pattern != 0U) ? 0x80U : 0x00U);
#ifdef CAM_DIAGNOSTICS
        sys_prob_snapshot_regs();
#endif
    }

    /* Sampling-edge change. CR configuration bits may only move while the DCMI
     * is disabled, so bounce the pipeline around the write. */
    if (s_cam_diag.ctl.pclk_pol != s_cam.applied_pclk_pol)
    {
        s_cam.applied_pclk_pol = s_cam_diag.ctl.pclk_pol;
        const bool was_running = s_cam.auto_running;

        if (was_running)
        {
            HAL_DCMI_Stop(&hdcmi);
        }
        hdcmi.Init.PCKPolarity = (s_cam_diag.ctl.pclk_pol != 0U) ? DCMI_PCKPOLARITY_RISING
                                                                 : DCMI_PCKPOLARITY_FALLING;
        MODIFY_REG(hdcmi.Instance->CR, DCMI_CR_PCKPOL, hdcmi.Init.PCKPolarity);

        if (was_running)
        {
            __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
            if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                                   (uint32_t)s_cam.fb_base, FRAME_SIZE / 4U) == HAL_OK)
            {
                s_cam_diag.stats.start_count++;
            }
            else
            {
                s_cam_diag.stats.start_fail_count++;
                s_cam.auto_running = false;
            }
            s_cam_diag.stats.last_frame_ms = HAL_GetTick();
        }
    }

    /* Debugger force-run: keep continuous capture alive with no USB host so the
     * frame buffer can be inspected over SWD (and PCLK/crop changes exercised
     * without a streaming client). */
    if ((s_cam_diag.ctl.force_run != 0U) && !s_cam.auto_running)
    {
        (void)bsp_camera_start_continuous((uint8_t (*)[FRAME_SIZE])s_cam.fb_base);
    }

    if (!s_cam.auto_running)
    {
        return;
    }

    const uint32_t ris = hdcmi.Instance->RISR;
    if (ris & DCMI_FLAG_OVRRI)
    {
        s_cam_diag.stats.ovr_count++;
        __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI);
    }
    if (ris & DCMI_FLAG_ERRRI)
    {
        s_cam_diag.stats.sync_err_count++;
        __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_ERRRI);
    }

    if ((HAL_GetTick() - s_cam_diag.stats.last_frame_ms) > CAM_WATCHDOG_MS)
    {
        s_cam_diag.stats.restart_count++;
        HAL_DCMI_Stop(&hdcmi);
        __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
        if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                               (uint32_t)s_cam.fb_base, FRAME_SIZE / 4U) != HAL_OK)
        {
            s_cam_diag.stats.start_fail_count++;
        }
        else
        {
            s_cam_diag.stats.start_count++;
        }
        s_cam_diag.stats.last_frame_ms = HAL_GetTick();
    }
}

/* Non-blocking poll: returns a pointer to the most recently completed frame, or
 * NULL if no new frame has landed since the last call/advance.
 *
 * In single-buffer circular mode the DMA writes into buf[0] forever; the
 * DCMI FRAME interrupt sets s_cam.frame_done=true on every VSYNC boundary. The
 * consumer reads buf[0] at its own pace — if it is slower than the sensor,
 * it simply sees a slightly stale but always valid frame. */
const uint8_t *bsp_camera_take_frame(void)
{
    if (s_cam.frame_done)
    {
        return s_cam.fb_base; /* == &s_fb_ext[0][0] == buf[0] */
    }
    return NULL;
}

/* Mark the current frame as consumed. Call once per successful take_frame(). */
void bsp_camera_advance(void)
{
    s_cam.frame_done = false;
}

/* ==========================================================================
 * Tear-free frame snapshot
 *
 * The capture DMA runs circular over a single buffer, so the one coherent
 * moment to copy a frame out is while the sensor is in vertical blanking:
 * NDTR has just reloaded to the full frame length and no new pixels are
 * landing yet.
 *
 * Copying at any other phase produces exactly the artefact a host sees on
 * fast motion. The copy starts at the top of the buffer, behind the DMA
 * write pointer, and runs ~20x faster than it; partway down it overtakes the
 * pointer. Everything before the crossing is frame N+1, everything after is
 * frame N - a stitched image with a seam wherever the DMA happened to be.
 * A static scene hides the seam because both frames are identical; a moving
 * one puts it on full display.
 *
 * Measured on this board: blanking is ~16 ms of an 83 ms frame (NDTR sits at
 * 0x7080 for 8 of every 41 samples), and the copy takes well under 2 ms, so
 * the margin is comfortable. Both ends are checked anyway - a copy that
 * strays out of the window is discarded rather than shipped.
 * ========================================================================== */

/* How far the DMA may advance into the next frame and still leave a seam too
 * small to see: 120 words == 480 bytes == one 240-pixel YUY2 row. */
#define CAM_SNAP_MARGIN_W 120U
/* True while the DCMI is between frames (write pointer parked at the top). */
bool bsp_camera_in_vblank(void)
{
    return __HAL_DMA_GET_COUNTER(&hdma_dcmi) >= (FRAME_SIZE / 4U);
}

bool bsp_camera_snapshot(uint8_t *dst)
{
    static bool dwt_ready = false;

    const uint32_t total = FRAME_SIZE / 4U;

    if ((dst == NULL) || (s_cam.fb_base == NULL) || !s_cam.frame_done)
    {
        return false;
    }

    /* Gate: refuse to start outside vertical blanking. s_cam.frame_done stays set so
     * the caller simply retries until the window opens. */
    const uint32_t n0 = __HAL_DMA_GET_COUNTER(&hdma_dcmi);
    if (n0 < total)
    {
        s_cam_diag.snap.snap_wait++;
        return false;
    }

    if (!dwt_ready)
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        dwt_ready = true;
    }

    const uint32_t t0 = DWT->CYCCNT;
    memcpy(dst, s_cam.fb_base, FRAME_SIZE);
    const uint32_t n1 = DWT->CYCCNT;

    s_cam_diag.snap.snap_cycles = n1 - t0;
    s_cam_diag.snap.snap_ndtr0  = n0;
    s_cam_diag.snap.snap_ndtr1  = __HAL_DMA_GET_COUNTER(&hdma_dcmi);

    /* This frame has been taken either way - a torn one is thrown away rather
     * than retried, otherwise a slow copy would spin on the same stale data. */
    s_cam.frame_done = false;

    if (s_cam_diag.snap.snap_ndtr1 < (total - CAM_SNAP_MARGIN_W))
    {
        s_cam_diag.snap.snap_torn++;
        return false;
    }

    s_cam_diag.snap.snap_ok++;
    return true;
}

void bsp_camera_set_buffers(uint8_t (*fb)[FRAME_SIZE])
{
    s_cam.fb_base = (uint8_t *)fb;
}

/* ==========================================================================
 * HAL callbacks
 * ========================================================================== */

/* DMA transfer-complete callback: called when one of the two buffers has been
 * fully written. In double-buffer mode this fires twice per frame (once for M0,
 * once for M1). We record which buffer just finished so the consumer can read
 * it without racing against the DMA engine. */
/* Fires once per VSYNC boundary. In CONTINUOUS mode the DMA has already
 * wrapped on its own, so there is nothing to restart here - we only publish
 * the frame and pet the watchdog.
 *
 * HAL_DCMI_IRQHandler() clears DCMI_IT_FRAME on the way in and relies on
 * DCMI_DMAXferCplt() to switch it back on. That hand-off only happens when
 * the DMA transfer-complete interrupt lands, so re-enable it here too: it
 * makes the pipeline independent of the TC-vs-FRAME arrival order. */
void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef *phdcmi)
{
    if (phdcmi->Instance == DCMI)
    {
        /* NDTR counts down and reloads at every circular wrap, so the distance
         * travelled since the previous frame is a modular subtraction. */
        const uint32_t ndtr            = __HAL_DMA_GET_COUNTER(&hdma_dcmi);
        const uint32_t total           = FRAME_SIZE / 4U;
        s_cam_diag.ctl.words_per_frame = (s_cam.prev_ndtr + total - ndtr) % total;
        if (s_cam_diag.ctl.words_per_frame == 0U && s_cam_diag.stats.frame_count != 0U)
        {
            s_cam_diag.ctl.words_per_frame = total; /* exact multiple of a full wrap */
        }
        s_cam.prev_ndtr = ndtr;

        s_cam_diag.stats.frame_count++;
        s_cam_diag.stats.last_frame_ms = HAL_GetTick();
        s_cam.frame_done               = true;
        __HAL_DCMI_ENABLE_IT(phdcmi, DCMI_IT_FRAME);
    }
}

/* Kept for API compatibility. Single-buffer circular capture needs no M0/M1
 * bookkeeping, and HAL_DCMI_Start_DMA() overwrites XferCpltCallback with its
 * own handler anyway, so installing ours here would be silently undone. */
void bsp_camera_link_dma_callbacks(void)
{
}

void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef *phdcmi)
{
    if (phdcmi->Instance == DCMI)
    {
        s_cam_diag.stats.error_count++;
    }
}
