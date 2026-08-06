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
#include "ov5640.h"
#include "ov5640_ref.h"
#include <string.h>

/* Chip ID reported by a healthy OV5640 (registers 0x300A/0x300B). */
#define OV5640_CHIP_ID  0x5640U

DCMI_HandleTypeDef hdcmi;
DMA_HandleTypeDef  hdma_dcmi;
I2C_HandleTypeDef  hi2c_cam;

volatile uint32_t cam_frame_count      = 0;
volatile uint32_t cam_error_count      = 0;
volatile uint32_t cam_start_count      = 0;
volatile uint32_t cam_start_fail_count = 0;
volatile uint32_t cam_ovr_count        = 0; /* DCMI FIFO overruns seen by polling  */
volatile uint32_t cam_sync_err_count   = 0; /* embedded-sync errors seen by polling*/
volatile uint32_t cam_restart_count    = 0; /* watchdog-driven pipeline restarts   */
volatile uint32_t cam_last_frame_ms    = 0; /* HAL tick of the last FRAME IRQ      */

static OV5640_Object_t  ov5640_obj;
static uint32_t         ov5640_id = 0;

/* ---- Sensor register visibility -----------------------------------------
 * Read back after OV5640_Start() and parked in RAM so the whole DVP setup can
 * be inspected over SWD. The pad-output-enable pair is the interesting one:
 *   0x3017 bit6 VSYNC, bit5 HREF, bit4 PCLK, bit[3:0] D[9:6]
 *   0x3018 bit[7:2] D[5:0]
 * Sync pads enabled while the data pads stay tri-stated gives correct frame
 * timing over a floating data bus - i.e. one drifting value per line. */
const uint16_t cam_reg_addr[CAM_REG_SNAP_N] = {
    0x3008, 0x300E, 0x3017, 0x3018, 0x3034, 0x3035, 0x3036, 0x3037,
    0x3108, 0x3821, 0x4300, 0x501F, 0x4740, 0x3808, 0x3809, 0x380A,
    0x380B, 0x503D,
};
volatile uint8_t cam_reg_val[CAM_REG_SNAP_N];

/* Poke from the debugger to drive the sensor's internal colour-bar generator:
 *   0 = normal imaging, 1 = colour bars (OV5640 reg 0x503D = 0x80).
 * A clean bar pattern proves the DCMI/DMA path and puts the blame on the
 * sensor's imaging config; stripes in bar mode blame the DVP wiring. */
volatile uint32_t cam_test_pattern = 0;
static uint32_t   s_applied_pattern = 0xFFFFFFFFU;

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
volatile uint32_t cam_pclk_pol = 1;
static uint32_t   s_applied_pclk_pol = 0xFFFFFFFFU;

/* ---- Physical-layer pin probe -------------------------------------------
 * Write cam_probe_req = 1 from the debugger to detach the 11 DVP signals from
 * the DCMI, sample them as raw GPIO inputs for a while, and report which ones
 * actually move. Bit order in all three masks:
 *   0..7 = D0..D7, 8 = HSYNC, 9 = VSYNC, 10 = PIXCLK
 * A line that never reaches 1 (or never reaches 0) is stuck - wiring, pad
 * enable or alternate-function problem - and no amount of DCMI tuning fixes
 * it. cam_pin_edges counts transitions so a slow line can be told from a fast
 * one. */
/* Crop on/off at runtime. With crop disabled the DCMI takes the sensor's full
 * 320x240 line instead of the centred 240-pixel window, which tells us whether
 * the crop window itself is landing somewhere useless. */
volatile uint32_t cam_crop_en = 1;
static uint32_t   s_applied_crop = 0xFFFFFFFFU;

/* Words actually delivered between two FRAME interrupts, derived from the
 * circular DMA counter. A correct 240x240 YUY2 capture must show exactly
 * 28800 words (115200 bytes); anything else means the DCMI is taking in a
 * different amount of data than the crop window claims. */
volatile uint32_t cam_words_per_frame = 0;
static volatile uint32_t s_prev_ndtr  = 0;

volatile uint32_t cam_probe_req   = 0;
volatile uint32_t cam_pin_ones    = 0;
volatile uint32_t cam_pin_zeros   = 0;
volatile uint32_t cam_pin_edges[11];
volatile uint32_t cam_probe_done  = 0;

/* Debugger-driven data-bus probe, bucketed by HSYNC state. Set cam_href_req
 * = 1 to sample D[7:0] for many whole lines and report, separately for the
 * HSYNC-high and HSYNC-low windows, how many distinct byte values appear and
 * a few of the values seen. Sync-edge counters tell us the line/frame timing.
 *
 *   cam_bus_hi_* : sampled while HSYNC was high (the active-line window as the
 *                  probe sees it)
 *   cam_bus_lo_* : sampled while HSYNC was low (blanking as the probe sees it)
 *
 * If BOTH windows are constant the sensor is not streaming pixel data at all
 * (blame its timing/sync config). If only one window changes, the DCMI's
 * HSPolarity is selecting the wrong one. */
volatile uint32_t cam_href_req    = 0;

/* Force continuous capture to run even with no USB host, so the frame buffer
 * can be inspected over SWD. Set cam_force_run = 1 from the debugger. The
 * DCMI keeps filling s_fb_base; uvc_app's own start/stop is unaffected while
 * the host is silent. */
volatile uint32_t cam_force_run   = 0;

/* ---- Raw DVP bus trace ---------------------------------------------------
 * The bucketed probe above samples freely and can only say "some byte value
 * changed at some point". It cannot answer the one question that matters once
 * the DCMI is known to be correctly framed: *while PIXCLK is toggling, does
 * the data bus move at all?*
 *
 * This capture locks onto an HSYNC rising edge, then reads GPIOA and GPIOE
 * back-to-back as fast as the core can issue loads (~8 ns per pair at 480 MHz,
 * i.e. at least two samples per PIXCLK period at any rate the OV5640 can
 * produce). Nothing is decoded on-target: the raw IDR words are parked in
 * DTCM and unpicked offline by debug/bustrace.py.
 *
 *   even slots = GPIOA->IDR   bit4 = HSYNC (PA4), bit6 = PIXCLK (PA6)
 *   odd  slots = GPIOE->IDR   bit4 = D4 (PE4), bit5 = D6 (PE5), bit6 = D7 (PE6)
 *
 * PIXCLK toggling thousands of times while D4/D6/D7 never move puts the fault
 * on the sensor's data pads or the DVP wiring; any data movement between clock
 * edges puts it back on the DCMI. */
volatile uint32_t cam_trace_req  = 0;
volatile uint32_t cam_trace_done = 0;
uint32_t          cam_trace[CAM_TRACE_N];

/* ---- Pull-resistor discrimination ---------------------------------------
 * The textbook way to tell "driven low" from "not connected", with no scope.
 * Sample the eleven DVP lines three times over - floating, pulled up, pulled
 * down - and AND/OR every sample together:
 *
 *   driven line  : the pulls (~40 kOhm) lose against the sensor's push-pull
 *                  output, so the value is identical in all three passes.
 *   floating line: follows whatever resistor is attached - reads all ones
 *                  under pull-up and all zeros under pull-down.
 *
 * The sync lines double as a positive control: they are known good, so they
 * must come out identical across the three passes. If they move too, the test
 * itself is wrong rather than the wiring.
 *
 * Index 0 = no pull, 1 = pull-up, 2 = pull-down.
 * Bit order matches dvp_sample(): 0..7 = D0..D7, 8 = HSYNC, 9 = VSYNC,
 * 10 = PIXCLK. */
volatile uint32_t cam_pull_req  = 0;
volatile uint32_t cam_pull_done = 0;
volatile uint32_t cam_pull_and[3];
volatile uint32_t cam_pull_or[3];

/* ---- Arbitrary SCCB register window -------------------------------------
 * The fixed snapshot list is too small to chase a timing-generator bug. Point
 * cam_regdump_base at any address, set cam_regdump_req = 1, and 64 consecutive
 * registers land in cam_regdump[]. 0xEE marks a failed read. */
volatile uint16_t cam_regdump_base = 0x3800;
volatile uint32_t cam_regdump_req  = 0;
volatile uint32_t cam_regdump_done = 0;
volatile uint8_t  cam_regdump[CAM_REGDUMP_N];

volatile uint8_t  cam_bus_hi_samples[16];
volatile uint8_t  cam_bus_lo_samples[16];
volatile uint32_t cam_bus_hi_distinct = 0;
volatile uint32_t cam_bus_lo_distinct = 0;
volatile uint32_t cam_bus_hi_count    = 0;
volatile uint32_t cam_bus_lo_count    = 0;
volatile uint32_t cam_href_edges      = 0;
volatile uint32_t cam_vsync_edges     = 0;

/* The DMA runs circular over a single frame buffer, so there is no ownership
 * hand-off to track: the consumer always reads s_fb_base and simply learns
 * from frame_done that a fresh VSYNC boundary went by. */
static volatile bool  frame_done     = false; /* set by DCMI FrameEventCallback */
static uint8_t       *s_fb_base      = NULL;  /* set by bsp_camera_set_buffers()*/
static volatile bool  s_auto_running = false; /* capture is meant to be running */

/* cam_flicker bookkeeping: which frame we last flipped on, and the current
 * polarity of the negative-image effect. */
static uint32_t s_flicker_fc = 0;
static bool     s_flicker_on = false;

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

  if (HAL_I2C_Init(&hi2c_cam) != HAL_OK) {
    return -1;
  }
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c_cam, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
    return -1;
  }
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c_cam, 0) != HAL_OK) {
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
                            pdata, len, 1000) == HAL_OK) ? 0 : -1;
}

static int32_t cam_i2c_read(uint16_t addr, uint16_t reg, uint8_t *pdata, uint16_t len)
{
  return (HAL_I2C_Mem_Read(&hi2c_cam, addr, reg, I2C_MEMADD_SIZE_16BIT,
                           pdata, len, 1000) == HAL_OK) ? 0 : -1;
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
  hdcmi.Init.PCKPolarity      = (cam_pclk_pol != 0U) ? DCMI_PCKPOLARITY_RISING
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

  if (HAL_DCMI_Init(&hdcmi) != HAL_OK) {
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
                                DCMI_IT_ERR  | DCMI_IT_OVR);

  const uint32_t x0    = ((CAM_SENSOR_WIDTH - FRAME_WIDTH) / 2U) * FRAME_BYTES_PER_PX;
  const uint32_t y0    = (CAM_SENSOR_HEIGHT - FRAME_HEIGHT) / 2U;
  const uint32_t xsize = (FRAME_WIDTH * FRAME_BYTES_PER_PX) - 1U;
  const uint32_t ysize = FRAME_HEIGHT - 1U;

  if (HAL_DCMI_ConfigCrop(&hdcmi, x0, y0, xsize, ysize) != HAL_OK) {
    return CAM_ERR_DCMI;
  }
  if (HAL_DCMI_EnableCrop(&hdcmi) != HAL_OK) {
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

  if (cam_i2c_init() != 0) {
    return CAM_ERR_I2C;
  }

  io.Init     = cam_i2c_init;
  io.DeInit   = cam_i2c_deinit;
  io.Address  = CAM_I2C_ADDRESS;
  io.WriteReg = cam_i2c_write;
  io.ReadReg  = cam_i2c_read;
  io.GetTick  = cam_get_tick;

  if (OV5640_RegisterBusIO(&ov5640_obj, &io) != OV5640_OK) {
    return CAM_ERR_I2C;
  }
  if (OV5640_ReadID(&ov5640_obj, &ov5640_id) != OV5640_OK) {
    return CAM_ERR_I2C;
  }
  if (ov5640_id != OV5640_CHIP_ID) {
    return CAM_ERR_ID;
  }
  /* The ST BSP tables are not used: OV5640_R320x240 programs a horizontal
   * sub-sample increment without enabling horizontal binning, which freezes
   * the column address generator and repeats one pixel across every line.
   * ov5640_ref_init() applies a sequence verified on this hardware and leaves
   * the sensor streaming (0x3008 = 0x02 is the last write). */
  if (ov5640_ref_init() != 0) {
    return CAM_ERR_SENSOR;
  }
  if (cam_dcmi_init() != CAM_OK) {
    return CAM_ERR_DCMI;
  }

  cam_snapshot_regs();

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
static inline uint32_t dvp_sample(void)
{
  const uint32_t a = GPIOA->IDR, c = GPIOC->IDR, d = GPIOD->IDR;
  const uint32_t e = GPIOE->IDR, g = GPIOG->IDR;

  return (((c >>  6) & 1U) <<  0) |  /* D0    PC6  */
         (((c >>  7) & 1U) <<  1) |  /* D1    PC7  */
         (((g >> 10) & 1U) <<  2) |  /* D2    PG10 */
         (((g >> 11) & 1U) <<  3) |  /* D3    PG11 */
         (((e >>  4) & 1U) <<  4) |  /* D4    PE4  */
         (((d >>  3) & 1U) <<  5) |  /* D5    PD3  */
         (((e >>  5) & 1U) <<  6) |  /* D6    PE5  */
         (((e >>  6) & 1U) <<  7) |  /* D7    PE6  */
         (((a >>  4) & 1U) <<  8) |  /* HSYNC PA4  */
         (((g >>  9) & 1U) <<  9) |  /* VSYNC PG9  */
         (((a >>  6) & 1U) << 10);   /* PCLK  PA6  */
}

/* Flip the DVP pins between DCMI alternate function and plain floating input. */
static void dvp_pins_mode(bool as_input)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Mode      = as_input ? GPIO_MODE_INPUT : GPIO_MODE_AF_PP;
  gpio.Pull      = GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF13_DCMI;

  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_6;                /* HSYNC, PIXCLK */
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;                /* D0, D1 */
  HAL_GPIO_Init(GPIOC, &gpio);
  gpio.Pin = GPIO_PIN_3;                             /* D5 */
  HAL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;   /* D4, D6, D7 */
  HAL_GPIO_Init(GPIOE, &gpio);
  gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11; /* VSYNC, D2, D3 */
  HAL_GPIO_Init(GPIOG, &gpio);
}

/* Re-init the eleven DVP lines as inputs with the requested pull setting. */
static void dvp_pins_pull(uint32_t pull)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Mode  = GPIO_MODE_INPUT;
  gpio.Pull  = pull;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_6;                /* HSYNC, PIXCLK */
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;                /* D0, D1 */
  HAL_GPIO_Init(GPIOC, &gpio);
  gpio.Pin = GPIO_PIN_3;                             /* D5 */
  HAL_GPIO_Init(GPIOD, &gpio);
  gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;   /* D4, D6, D7 */
  HAL_GPIO_Init(GPIOE, &gpio);
  gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11; /* VSYNC, D2, D3 */
  HAL_GPIO_Init(GPIOG, &gpio);
}

void bsp_camera_probe_pull(void)
{
  static const uint32_t pulls[3] = {
    GPIO_NOPULL, GPIO_PULLUP, GPIO_PULLDOWN,
  };
  const bool was_running = s_auto_running;

  if (was_running) {
    bsp_camera_stop();
  }

  for (uint32_t k = 0; k < 3U; k++) {
    dvp_pins_pull(pulls[k]);
    HAL_Delay(2); /* let the ~40 kOhm pull settle against the pin capacitance */

    uint32_t acc_and = 0x7FFU;
    uint32_t acc_or  = 0U;
    for (uint32_t i = 0; i < 200000U; i++) {
      const uint32_t v = dvp_sample();
      acc_and &= v;
      acc_or  |= v;
    }
    cam_pull_and[k] = acc_and & 0x7FFU;
    cam_pull_or[k]  = acc_or & 0x7FFU;
  }

  dvp_pins_mode(false);
  if (was_running) {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                           (uint32_t)s_fb_base, FRAME_SIZE / 4U) == HAL_OK) {
      s_auto_running    = true;
      cam_last_frame_ms = HAL_GetTick();
      cam_start_count++;
    }
  }
  cam_pull_done++;
}

void bsp_camera_probe_pins(void)
{
  const bool was_running = s_auto_running;

  if (was_running) {
    bsp_camera_stop();
  }
  dvp_pins_mode(true);

  uint32_t ones = 0, zeros = 0;
  uint32_t edges[11] = {0};
  uint32_t prev = dvp_sample();

  /* ~400 k samples: at 480 MHz this spans several milliseconds, i.e. many
   * whole lines, so even the slowest signal (VSYNC) gets a chance to move. */
  for (uint32_t i = 0; i < 400000U; i++) {
    const uint32_t v = dvp_sample();
    ones  |= v;
    zeros |= ~v;
    const uint32_t ch = v ^ prev;
    if (ch) {
      for (uint32_t b = 0; b < 11U; b++) {
        if (ch & (1U << b)) {
          edges[b]++;
        }
      }
      prev = v;
    }
  }

  cam_pin_ones  = ones & 0x7FFU;
  cam_pin_zeros = zeros & 0x7FFU;
  for (uint32_t b = 0; b < 11U; b++) {
    cam_pin_edges[b] = edges[b];
  }

  dvp_pins_mode(false);
  if (was_running) {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                           (uint32_t)s_fb_base, FRAME_SIZE / 4U) == HAL_OK) {
      s_auto_running    = true;
      cam_last_frame_ms = HAL_GetTick();
      cam_start_count++;
    }
  }
  cam_probe_done++;
}

/* Sample D[7:0] for the duration of a single HREF-active window.
 *
 * The free-running pin probe cannot tell "bus is busy during blanking" from
 * "bus carries pixels", because it averages over both. This one locks onto a
 * line and answers the only question left: while the DCMI considers the data
 * valid, does the bus actually change? */
void bsp_camera_probe_href(void)
{
  const bool was_running = s_auto_running;

  if (was_running) {
    bsp_camera_stop();
  }
  dvp_pins_mode(true);

  static uint8_t seen_hi[256];
  static uint8_t seen_lo[256];
  for (uint32_t i = 0; i < 256U; i++) {
    seen_hi[i] = 0;
    seen_lo[i] = 0;
  }

  uint32_t hi_count = 0, lo_count = 0;
  uint32_t hi_distinct = 0, lo_distinct = 0;
  uint32_t href_edges = 0, vsync_edges = 0;
  uint32_t prev_href = 0, prev_vsync = 0;
  uint32_t hi_written = 0, lo_written = 0;

  /* ~600 k samples spans several whole frames at any plausible PCLK rate. */
  for (uint32_t i = 0; i < 600000U; i++) {
    const uint32_t a = GPIOA->IDR;
    const uint32_t c = GPIOC->IDR;
    const uint32_t dd = GPIOD->IDR;
    const uint32_t e = GPIOE->IDR;
    const uint32_t g = GPIOG->IDR;

    const uint8_t href  = (uint8_t)((a >> 4) & 1U);   /* PA4  */
    const uint8_t vsync = (uint8_t)((g >> 9) & 1U);   /* PG9  */
    const uint8_t v = (uint8_t)((((c >>  6) & 1U) << 0) |
                                (((c >>  7) & 1U) << 1) |
                                (((g >> 10) & 1U) << 2) |
                                (((g >> 11) & 1U) << 3) |
                                (((e >>  4) & 1U) << 4) |
                                (((dd >>  3) & 1U) << 5) |
                                (((e >>  5) & 1U) << 6) |
                                (((e >>  6) & 1U) << 7));

    if (href != 0U) {
      hi_count++;
      if (seen_hi[v] == 0U) {
        seen_hi[v] = 1U;
        hi_distinct++;
      }
      if (hi_written < 16U) {
        cam_bus_hi_samples[hi_written++] = v;
      }
    } else {
      lo_count++;
      if (seen_lo[v] == 0U) {
        seen_lo[v] = 1U;
        lo_distinct++;
      }
      if (lo_written < 16U) {
        cam_bus_lo_samples[lo_written++] = v;
      }
    }

    if (href != prev_href) {
      href_edges++;
      prev_href = href;
    }
    if (vsync != prev_vsync) {
      vsync_edges++;
      prev_vsync = vsync;
    }
  }

  cam_bus_hi_count    = hi_count;
  cam_bus_lo_count    = lo_count;
  cam_bus_hi_distinct = hi_distinct;
  cam_bus_lo_distinct = lo_distinct;
  cam_href_edges      = href_edges;
  cam_vsync_edges     = vsync_edges;

  dvp_pins_mode(false);
  if (was_running) {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                           (uint32_t)s_fb_base, FRAME_SIZE / 4U) == HAL_OK) {
      s_auto_running    = true;
      cam_last_frame_ms = HAL_GetTick();
      cam_start_count++;
    }
  }
  cam_probe_done++;
}

/* Free-run the DVP bus into DTCM at maximum load bandwidth. See the comment
 * next to cam_trace[] for the slot layout and what the result proves. */
void bsp_camera_trace_bus(void)
{
  const bool was_running = s_auto_running;

  if (was_running) {
    bsp_camera_stop();
  }
  dvp_pins_mode(true);

  volatile const uint32_t *const pa = &GPIOA->IDR;
  volatile const uint32_t *const pe = &GPIOE->IDR;
  uint32_t *p = cam_trace;

  __disable_irq();

  /* Start the window on a line boundary: wait for HSYNC to be low, then for
   * the rising edge. Bounded so a dead sync line cannot hang the main loop. */
  for (uint32_t g = 4000000U; g != 0U; g--) {
    if ((((*pa) >> 4) & 1U) == 0U) {
      break;
    }
  }
  for (uint32_t g = 4000000U; g != 0U; g--) {
    if ((((*pa) >> 4) & 1U) != 0U) {
      break;
    }
  }

  /* Unrolled by eight so the loop overhead does not eat into the sample rate.
   * Two loads and two stores per pair, no decoding whatsoever. */
  for (uint32_t i = 0; i < CAM_TRACE_N; i += 8U) {
    p[0] = *pa; p[1] = *pe;
    p[2] = *pa; p[3] = *pe;
    p[4] = *pa; p[5] = *pe;
    p[6] = *pa; p[7] = *pe;
    p += 8;
  }

  __enable_irq();

  dvp_pins_mode(false);
  if (was_running) {
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                           (uint32_t)s_fb_base, FRAME_SIZE / 4U) == HAL_OK) {
      s_auto_running    = true;
      cam_last_frame_ms = HAL_GetTick();
      cam_start_count++;
    }
  }
  cam_trace_done++;
}

void bsp_camera_regdump(void)
{
  const uint16_t base = cam_regdump_base;

  for (uint32_t i = 0; i < CAM_REGDUMP_N; i++) {
    uint8_t v = 0;
    if (bsp_camera_read_reg((uint16_t)(base + i), &v) != 0) {
      v = 0xEE;
    }
    cam_regdump[i] = v;
  }
  cam_regdump_done++;
}

void cam_snapshot_regs(void)
{
  for (uint32_t i = 0; i < CAM_REG_SNAP_N; i++) {
    uint8_t v = 0;
    if (bsp_camera_read_reg(cam_reg_addr[i], &v) != 0) {
      v = 0xEE; /* marker: the I2C read itself failed */
    }
    cam_reg_val[i] = v;
  }
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
                         (uint32_t)buf[0], FRAME_SIZE / 4U) != HAL_OK) {
    cam_start_fail_count++;
    s_auto_running = false;
    return CAM_ERR_DCMI;
  }

  s_auto_running    = true;
  frame_done        = false;
  cam_last_frame_ms = HAL_GetTick();
  cam_start_count++;
  return CAM_OK;
}

void bsp_camera_stop(void)
{
  s_auto_running = false;
  HAL_DCMI_Stop(&hdcmi);
  frame_done = false;
}

/* Main-loop housekeeping. Must be called regularly while streaming.
 *
 *  1. Drains the OVR / sync-error flags that we deliberately left un-masked
 *     at NVIC level, so they are recorded but never abort the DMA.
 *  2. Watchdog: a wedged sensor (or a genuinely aborted DMA) shows up as
 *     "no FRAME interrupt for CAM_WATCHDOG_MS". Rebuild the pipeline instead
 *     of streaming a frozen image forever. */
void bsp_camera_service(void)
{
  if (cam_probe_req != 0U) {
    cam_probe_req = 0;
    bsp_camera_probe_pins();
  }

  if (cam_href_req != 0U) {
    cam_href_req = 0;
    bsp_camera_probe_href();
  }

  if (cam_trace_req != 0U) {
    cam_trace_req = 0;
    bsp_camera_trace_bus();
  }

  if (cam_pull_req != 0U) {
    cam_pull_req = 0;
    bsp_camera_probe_pull();
  }

  if (cam_regdump_req != 0U) {
    cam_regdump_req = 0;
    bsp_camera_regdump();
  }

  /* Debug: drive the snapshot path with no USB host attached, so the tear
   * telemetry and the resulting buffer can both be inspected over SWD.
   * Destination is the first transmit-side buffer, s_fb_base + FRAME_SIZE. */
  if ((cam_snap_test != 0U) && s_auto_running && (s_fb_base != NULL)) {
    (void)bsp_camera_snapshot(s_fb_base + FRAME_SIZE);
  }

  /* Debug A/B control: one unsynchronised copy into the *second* transmit
   * buffer, taken at whatever phase the DMA happens to be at. This is what
   * the code used to do on every frame, so dumping both buffers from one run
   * shows the fix and the bug side by side. */
  if ((cam_snap_unsync != 0U) && s_auto_running && (s_fb_base != NULL)) {
    cam_snap_unsync = 0;
    cam_unsync_ndtr = __HAL_DMA_GET_COUNTER(&hdma_dcmi);
    memcpy(s_fb_base + 2U * FRAME_SIZE, s_fb_base, FRAME_SIZE);
  }

  /* Debug: flip the sensor between normal and negative on every frame. No
   * real scene can change more violently than that, so a snapshot that spans
   * two frames stops being a subtle seam and becomes a full-scale luma step
   * at the splice - trivially measurable from a single dumped frame. */
  if (cam_flicker != 0U) {
    if (cam_frame_count != s_flicker_fc) {
      s_flicker_fc = cam_frame_count;
      s_flicker_on = !s_flicker_on;
      (void)bsp_camera_write_reg((uint16_t)cam_flicker_reg,
                                 (uint8_t)(s_flicker_on ? cam_flicker_b
                                                        : cam_flicker_a));
    }
  }

  if (cam_poke_req != 0U) {
    cam_poke_req = 0;
    (void)bsp_camera_write_reg((uint16_t)cam_poke_reg, (uint8_t)cam_poke_val);
    cam_poke_done++;
  }

  if (cam_crop_en != s_applied_crop) {
    s_applied_crop = cam_crop_en;
    const bool was_running = s_auto_running;

    if (was_running) {
      HAL_DCMI_Stop(&hdcmi);
    }
    if (cam_crop_en != 0U) {
      (void)HAL_DCMI_EnableCrop(&hdcmi);
    } else {
      (void)HAL_DCMI_DisableCrop(&hdcmi);
    }
    if (was_running) {
      __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
      if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                             (uint32_t)s_fb_base, FRAME_SIZE / 4U) == HAL_OK) {
        cam_start_count++;
      } else {
        cam_start_fail_count++;
        s_auto_running = false;
      }
      cam_last_frame_ms = HAL_GetTick();
    }
  }

  /* Debugger-driven test-pattern toggle. Handled before the running check so
   * it can be armed while the pipeline is still idle. */
  if (cam_test_pattern != s_applied_pattern) {
    s_applied_pattern = cam_test_pattern;
    (void)bsp_camera_write_reg(0x503D, (cam_test_pattern != 0U) ? 0x80U : 0x00U);
    cam_snapshot_regs();
  }

  /* Sampling-edge change. CR configuration bits may only move while the DCMI
   * is disabled, so bounce the pipeline around the write. */
  if (cam_pclk_pol != s_applied_pclk_pol) {
    s_applied_pclk_pol = cam_pclk_pol;
    const bool was_running = s_auto_running;

    if (was_running) {
      HAL_DCMI_Stop(&hdcmi);
    }
    hdcmi.Init.PCKPolarity = (cam_pclk_pol != 0U) ? DCMI_PCKPOLARITY_RISING
                                                  : DCMI_PCKPOLARITY_FALLING;
    MODIFY_REG(hdcmi.Instance->CR, DCMI_CR_PCKPOL, hdcmi.Init.PCKPolarity);

    if (was_running) {
      __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
      if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                             (uint32_t)s_fb_base, FRAME_SIZE / 4U) == HAL_OK) {
        cam_start_count++;
      } else {
        cam_start_fail_count++;
        s_auto_running = false;
      }
      cam_last_frame_ms = HAL_GetTick();
    }
  }

  /* Debugger force-run: keep continuous capture alive with no USB host so the
   * frame buffer can be inspected over SWD (and PCLK/crop changes exercised
   * without a streaming client). */
  if ((cam_force_run != 0U) && !s_auto_running) {
    (void)bsp_camera_start_continuous((uint8_t(*)[FRAME_SIZE])s_fb_base);
  }

  if (!s_auto_running) {
    return;
  }

  const uint32_t ris = hdcmi.Instance->RISR;
  if (ris & DCMI_FLAG_OVRRI) {
    cam_ovr_count++;
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI);
  }
  if (ris & DCMI_FLAG_ERRRI) {
    cam_sync_err_count++;
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_ERRRI);
  }

  if ((HAL_GetTick() - cam_last_frame_ms) > CAM_WATCHDOG_MS) {
    cam_restart_count++;
    HAL_DCMI_Stop(&hdcmi);
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI);
    if (HAL_DCMI_Start_DMA(&hdcmi, DCMI_MODE_CONTINUOUS,
                           (uint32_t)s_fb_base, FRAME_SIZE / 4U) != HAL_OK) {
      cam_start_fail_count++;
    } else {
      cam_start_count++;
    }
    cam_last_frame_ms = HAL_GetTick();
  }
}

/* Non-blocking poll: returns a pointer to the most recently completed frame, or
 * NULL if no new frame has landed since the last call/advance.
 *
 * In single-buffer circular mode the DMA writes into buf[0] forever; the
 * DCMI FRAME interrupt sets frame_done=true on every VSYNC boundary. The
 * consumer reads buf[0] at its own pace — if it is slower than the sensor,
 * it simply sees a slightly stale but always valid frame. */
const uint8_t *bsp_camera_take_frame(void)
{
  if (frame_done) {
    return s_fb_base; /* == &s_fb_ext[0][0] == buf[0] */
  }
  return NULL;
}

/* Mark the current frame as consumed. Call once per successful take_frame(). */
void bsp_camera_advance(void)
{
  frame_done = false;
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
#define CAM_SNAP_MARGIN_W  120U

volatile uint32_t cam_snap_test   = 0; /* 1 = run snapshots without a host    */
volatile uint32_t cam_snap_unsync = 0; /* 1 = one deliberately torn copy (A/B)*/
volatile uint32_t cam_unsync_ndtr = 0; /* DMA position that torn copy saw     */

/* Generic SCCB poke, so any register can be tried from the debugger without
 * a reflash: set cam_poke_reg / cam_poke_val, then cam_poke_req = 1. */
volatile uint32_t cam_poke_req  = 0;
volatile uint32_t cam_poke_reg  = 0;
volatile uint32_t cam_poke_val  = 0;
volatile uint32_t cam_poke_done = 0;

/* Which register cam_flicker alternates, and between which two values. */
volatile uint32_t cam_flicker_reg = 0x5580U;
volatile uint32_t cam_flicker_a   = 0x06U;
volatile uint32_t cam_flicker_b   = 0x46U;
volatile uint32_t cam_flicker     = 0; /* 1 = invert every other sensor frame */
volatile uint32_t cam_snap_ok     = 0; /* coherent frames handed out          */
volatile uint32_t cam_snap_wait   = 0; /* polls deferred: not in blanking yet */
volatile uint32_t cam_snap_torn   = 0; /* DMA caught up mid-copy -> discarded */
volatile uint32_t cam_snap_ndtr0  = 0; /* NDTR when the last copy started     */
volatile uint32_t cam_snap_ndtr1  = 0; /* NDTR when the last copy finished    */
volatile uint32_t cam_snap_cycles = 0; /* CPU cycles the last copy took       */

/* True while the DCMI is between frames (write pointer parked at the top). */
bool bsp_camera_in_vblank(void)
{
  return __HAL_DMA_GET_COUNTER(&hdma_dcmi) >= (FRAME_SIZE / 4U);
}

bool bsp_camera_snapshot(uint8_t *dst)
{
  static bool dwt_ready = false;

  const uint32_t total = FRAME_SIZE / 4U;

  if ((dst == NULL) || (s_fb_base == NULL) || !frame_done) {
    return false;
  }

  /* Gate: refuse to start outside vertical blanking. frame_done stays set so
   * the caller simply retries until the window opens. */
  const uint32_t n0 = __HAL_DMA_GET_COUNTER(&hdma_dcmi);
  if (n0 < total) {
    cam_snap_wait++;
    return false;
  }

  if (!dwt_ready) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    dwt_ready = true;
  }

  const uint32_t t0 = DWT->CYCCNT;
  memcpy(dst, s_fb_base, FRAME_SIZE);
  const uint32_t n1 = DWT->CYCCNT;

  cam_snap_cycles = n1 - t0;
  cam_snap_ndtr0  = n0;
  cam_snap_ndtr1  = __HAL_DMA_GET_COUNTER(&hdma_dcmi);

  /* This frame has been taken either way - a torn one is thrown away rather
   * than retried, otherwise a slow copy would spin on the same stale data. */
  frame_done = false;

  if (cam_snap_ndtr1 < (total - CAM_SNAP_MARGIN_W)) {
    cam_snap_torn++;
    return false;
  }

  cam_snap_ok++;
  return true;
}

void bsp_camera_set_buffers(uint8_t (*fb)[FRAME_SIZE])
{
  s_fb_base = (uint8_t *)fb;
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
  if (phdcmi->Instance == DCMI) {
    /* NDTR counts down and reloads at every circular wrap, so the distance
     * travelled since the previous frame is a modular subtraction. */
    const uint32_t ndtr  = __HAL_DMA_GET_COUNTER(&hdma_dcmi);
    const uint32_t total = FRAME_SIZE / 4U;
    cam_words_per_frame  = (s_prev_ndtr + total - ndtr) % total;
    if (cam_words_per_frame == 0U && cam_frame_count != 0U) {
      cam_words_per_frame = total; /* exact multiple of a full wrap */
    }
    s_prev_ndtr = ndtr;

    cam_frame_count++;
    cam_last_frame_ms = HAL_GetTick();
    frame_done        = true;
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
  if (phdcmi->Instance == DCMI) {
    cam_error_count++;
  }
}
