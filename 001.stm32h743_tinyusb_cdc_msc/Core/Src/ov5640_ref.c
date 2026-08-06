/**
 ******************************************************************************
 * @file    ov5640_ref.c
 * @brief   OV5640 bring-up sequence ported from a known-good STM32H7 project.
 *
 * Why this replaces the ST BSP component driver
 * ---------------------------------------------
 * The ST BSP OV5640_R320x240 table left the sensor in an illegal state:
 * 0x3814 (horizontal sub-sample increment) was 0x31 - i.e. "take every other
 * column" - while 0x3821 bit0 (horizontal binning) stayed 0. The column
 * address generator then never advances, so the DVP repeats one pixel for the
 * whole line. Frame timing stays perfect (VSYNC/HREF/PCLK all correct, exactly
 * 28800 words per frame reach the DMA), which is why this looked like a DCMI
 * fault for so long: the bus really was static, DCMI sampled it faithfully.
 *
 * The register table below comes from a project verified on the same
 * STM32H743 + OV5640 hardware. Two values are changed for UVC:
 *
 *   0x4300  0x6F -> 0x30   DVP output RGB565 -> YUV422, YUYV byte order
 *   0x501F  0x01 -> 0x00   ISP output format RGB -> YUV422
 *
 * Everything else - PLL, ISP window, binning, BLC, AEC/AGC, AWB, gamma, LENC -
 * is kept byte for byte, including the write-then-read-back verification.
 *
 * Geometry: the sensor is left at the proven 4:3 ISP window and told to output
 * 400x300; the DCMI crop window then carves the 240x240 UVC frame out of it.
 ******************************************************************************
 */

#include "ov5640_ref.h"
#include "bsp_camera.h"

/* Sensor-side output size. The DCMI crop window trims this to FRAME_WIDTH x
 * FRAME_HEIGHT; keeping the sensor at 4:3 preserves the aspect ratio that the
 * ISP window and PLL timing were tuned for. */
#define OV5640_REF_OUT_W  400U
#define OV5640_REF_OUT_H  300U

#define OV5640_GROUP_ACCESS   0x3212U
#define OV5640_DVPHO_H        0x3808U
#define OV5640_DVPHO_L        0x3809U
#define OV5640_DVPVO_H        0x380AU
#define OV5640_DVPVO_L        0x380BU

/* Number of table entries that may fail read-back verification before we give
 * up. Some registers are write-only or auto-clearing; the proven table has
 * none of those, so any mismatch is a real bus problem. */
static const uint16_t ov5640_ref_config[][2] = {
    {0x3008, 0x42}, /* software power down - hold the sensor while we configure */
    {0x3103, 0x03}, /* system clock from PLL                                    */
    {0x3017, 0xff}, /* pad output enable: PCLK, VSYNC, HREF, D[9:6]             */
    {0x4740, 0x21}, /* PCLK/VSYNC/HREF polarity (matches DCMI RISING/LOW/LOW)   */
    {0x3018, 0xff}, /* pad output enable: D[5:0]                                */

    /* ---- clock tree, XVCLK = 24 MHz -------------------------------------- */
    {0x3037, 0x13}, /* PLL pre-divider /3 -> 8 MHz, R divider /2               */
    {0x3036, 0x64}, /* PLL multiplier x100 -> 800 MHz                          */
    {0x3035, 0x11}, /* system divider /1                                       */
    {0x3034, 0x1a}, /* MIPI 10-bit mode, PLL charge pump default               */
    {0x3108, 0x01}, /* PCLK root divider /1, SCLK root divider                 */
    {0x460c, 0x20}, /* DVP PCLK auto mode (0x3824 divider not used)            */
    {0x3824, 0x02}, /* manual PCLK divider - inert while 0x460c bit1 = 0       */

    /* ---- undocumented analogue / timing trim from the OV5640 app note ----- */
    {0x3630, 0x36}, {0x3631, 0x0e}, {0x3632, 0xe2}, {0x3633, 0x12},
    {0x3621, 0xe0}, {0x3704, 0xa0}, {0x3703, 0x5a}, {0x3715, 0x78},
    {0x3717, 0x01}, {0x370b, 0x60}, {0x3705, 0x1a}, {0x3905, 0x02},
    {0x3906, 0x10}, {0x3901, 0x0a}, {0x3731, 0x12}, {0x3600, 0x08},
    {0x3601, 0x33}, {0x302d, 0x60}, {0x3620, 0x52}, {0x371b, 0x20},
    {0x471c, 0x50}, {0x3635, 0x13}, {0x3636, 0x03}, {0x3634, 0x40},
    {0x3622, 0x01}, {0x440e, 0x00}, {0x5025, 0x00}, {0x3618, 0x00},
    {0x3612, 0x29}, {0x3708, 0x64}, {0x3709, 0x52}, {0x370c, 0x03},
    {0x302e, 0x00}, {0x460b, 0x37},

    /* ---- system blocks and clock gating ---------------------------------- */
    {0x3000, 0x00}, /* release reset on all blocks                             */
    {0x3002, 0x1c}, /* reset JFIFO / SFIFO / JPEG                              */
    {0x3004, 0xff}, /* enable all block clocks                                 */
    {0x3006, 0xc3}, /* gate off the JPEG clocks                                */
    {0x300e, 0x58}, /* MIPI off, DVP interface on                              */

    /* ---- output format: YUV422 / YUYV for UVC ---------------------------- */
    {0x4300, 0x30}, /* DVP format YUV422, sequence Y U Y V                     */
    {0x501f, 0x00}, /* ISP output format YUV422                                */
    {0x5000, 0xa7}, /* ISP: LENC, BPC, WPC, CIP on                             */
    {0x5001, 0xa3}, /* ISP: SDE, scaling, colour matrix, AWB on                */

    {0x3820, 0x47}, /* vertical binning enable + flip control                  */
    {0x3821, 0x01}, /* HORIZONTAL BINNING ENABLE - pairs with 0x3814 = 0x31    */

    /* ---- ISP input window: full 2624x1944 array -------------------------- */
    {0x3800, 0x00}, {0x3801, 0x00}, /* HS = 0                                  */
    {0x3802, 0x00}, {0x3803, 0x04}, /* VS = 4                                  */
    {0x3804, 0x0a}, {0x3805, 0x3f}, /* HE = 2623                               */
    {0x3806, 0x07}, {0x3807, 0x9b}, /* VE = 1947                               */
    {0x380c, 0x07}, {0x380d, 0x68}, /* HTS = 1896                              */
    {0x380e, 0x03}, {0x380f, 0xd8}, /* VTS = 984                               */

    /* ---- pre-scaling offsets and sub-sample increments ------------------- */
    {0x3810, 0x00}, {0x3811, 0x10}, /* horizontal offset 16                    */
    {0x3812, 0x00}, {0x3813, 0x04}, /* vertical offset 4                       */
    {0x3814, 0x31}, /* X increment - only legal with 0x3821 bit0 set           */
    {0x3815, 0x31}, /* Y increment - only legal with 0x3820 bit0 set           */

    /* ---- black level calibration ----------------------------------------- */
    {0x4001, 0x02}, {0x4004, 0x02}, {0x4005, 0x1a},

    /* ---- exposure -------------------------------------------------------- */
    {0x3a02, 0x05}, {0x3a03, 0xc4}, {0x3a08, 0x00}, {0x3a09, 0x93},
    {0x3a0a, 0x00}, {0x3a0b, 0x7b}, {0x3a0d, 0x08}, {0x3a0e, 0x06},
    {0x3a14, 0x05}, {0x3a15, 0xc4},

    /* ---- AEC gain -------------------------------------------------------- */
    {0x3a13, 0x43}, {0x3a18, 0x00}, {0x3a19, 0xf8},

    /* ---- 50/60 Hz banding filter ----------------------------------------- */
    {0x3c01, 0x34}, {0x3c04, 0x28}, {0x3c05, 0x98}, {0x3c06, 0x00},
    {0x3c07, 0x08}, {0x3c08, 0x00}, {0x3c09, 0x1c}, {0x3c0a, 0x9c},
    {0x3c0b, 0x40},

    /* ---- auto white balance ---------------------------------------------- */
    {0x5180, 0xff}, {0x5181, 0xf2}, {0x5182, 0x00}, {0x5183, 0x14},
    {0x5184, 0x25}, {0x5185, 0x24}, {0x5186, 0x09}, {0x5187, 0x09},
    {0x5188, 0x09}, {0x5189, 0x75}, {0x518a, 0x54}, {0x518b, 0xe0},
    {0x518c, 0xb2}, {0x518d, 0x42}, {0x518e, 0x3d}, {0x518f, 0x56},
    {0x5190, 0x46}, {0x5191, 0xf8}, {0x5192, 0x04}, {0x5193, 0x70},
    {0x5194, 0xf0}, {0x5195, 0xf0}, {0x5196, 0x03}, {0x5197, 0x01},
    {0x5198, 0x04}, {0x5199, 0x12}, {0x519a, 0x04}, {0x519b, 0x00},
    {0x519c, 0x06}, {0x519d, 0x82}, {0x519e, 0x38},

    /* ---- colour matrix --------------------------------------------------- */
    {0x5381, 0x1e}, {0x5382, 0x5b}, {0x5383, 0x08}, {0x5384, 0x0a},
    {0x5385, 0x7e}, {0x5386, 0x88}, {0x5387, 0x7c}, {0x5388, 0x6c},
    {0x5389, 0x10}, {0x538a, 0x01}, {0x538b, 0x98},

    /* ---- CIP sharpen / denoise ------------------------------------------- */
    {0x5300, 0x08}, {0x5301, 0x30}, {0x5302, 0x10}, {0x5303, 0x00},
    {0x5304, 0x08}, {0x5305, 0x30}, {0x5306, 0x08}, {0x5307, 0x16},
    {0x5309, 0x08}, {0x530a, 0x30}, {0x530b, 0x04}, {0x530c, 0x06},

    /* ---- gamma ----------------------------------------------------------- */
    {0x5480, 0x01}, {0x5481, 0x08}, {0x5482, 0x14}, {0x5483, 0x28},
    {0x5484, 0x51}, {0x5485, 0x65}, {0x5486, 0x71}, {0x5487, 0x7d},
    {0x5488, 0x87}, {0x5489, 0x91}, {0x548a, 0x9a}, {0x548b, 0xaa},
    {0x548c, 0xb8}, {0x548d, 0xcd}, {0x548e, 0xdd}, {0x548f, 0xea},
    {0x5490, 0x1d},

    /* ---- UV adjust ------------------------------------------------------- */
    {0x5580, 0x06}, {0x5583, 0x40}, {0x5584, 0x10}, {0x5589, 0x10},
    {0x558a, 0x00}, {0x558b, 0xf8}, {0x501d, 0x40},

    /* ---- AEC target / control -------------------------------------------- */
    {0x3a0f, 0x30}, {0x3a10, 0x28}, {0x3a1b, 0x30}, {0x3a1e, 0x26},
    {0x3a11, 0x60}, {0x3a1f, 0x14},

    /* ---- AWB manual gain (auto mode) ------------------------------------- */
    {0x3406, 0x00}, {0x3400, 0x04}, {0x3401, 0x00}, {0x3402, 0x04},
    {0x3403, 0x00}, {0x3404, 0x04}, {0x3405, 0x00},

    /* ---- lens shading correction ----------------------------------------- */
    {0x5800, 0x23}, {0x5801, 0x14}, {0x5802, 0x0f}, {0x5803, 0x0f},
    {0x5804, 0x12}, {0x5805, 0x26}, {0x5806, 0x0c}, {0x5807, 0x08},
    {0x5808, 0x05}, {0x5809, 0x05}, {0x580a, 0x08}, {0x580b, 0x0d},
    {0x580c, 0x08}, {0x580d, 0x03}, {0x580e, 0x00}, {0x580f, 0x00},
    {0x5810, 0x03}, {0x5811, 0x09}, {0x5812, 0x07}, {0x5813, 0x03},
    {0x5814, 0x00}, {0x5815, 0x01}, {0x5816, 0x03}, {0x5817, 0x08},
    {0x5818, 0x0d}, {0x5819, 0x08}, {0x581a, 0x05}, {0x581b, 0x06},
    {0x581c, 0x08}, {0x581d, 0x0e}, {0x581e, 0x29}, {0x581f, 0x17},
    {0x5820, 0x11}, {0x5821, 0x11}, {0x5822, 0x15}, {0x5823, 0x28},
    {0x5824, 0x46}, {0x5825, 0x26}, {0x5826, 0x08}, {0x5827, 0x26},
    {0x5828, 0x64}, {0x5829, 0x26}, {0x582a, 0x24}, {0x582b, 0x22},
    {0x582c, 0x24}, {0x582d, 0x24}, {0x582e, 0x06}, {0x582f, 0x22},
    {0x5830, 0x40}, {0x5831, 0x42}, {0x5832, 0x24}, {0x5833, 0x26},
    {0x5834, 0x24}, {0x5835, 0x22}, {0x5836, 0x22}, {0x5837, 0x26},
    {0x5838, 0x44}, {0x5839, 0x24}, {0x583a, 0x26}, {0x583b, 0x28},
    {0x583c, 0x42}, {0x583d, 0xce},

    {0x3008, 0x02}, /* wake from software power down - streaming starts here   */
};

#define OV5640_REF_CONFIG_N (sizeof(ov5640_ref_config) / sizeof(ov5640_ref_config[0]))

/* Index of the table entry that failed read-back, or -1 when all verified.
 * Exposed so a debugger can pinpoint an SCCB fault without a UART. */
volatile int32_t  ov5640_ref_fail_index = -1;
volatile uint16_t ov5640_ref_fail_reg   = 0;
volatile uint8_t  ov5640_ref_fail_want  = 0;
volatile uint8_t  ov5640_ref_fail_got   = 0;

static int32_t ov5640_ref_apply_table(void)
{
  uint8_t readback;

  for (uint32_t i = 0; i < OV5640_REF_CONFIG_N; i++) {
    const uint16_t reg = ov5640_ref_config[i][0];
    const uint8_t  val = (uint8_t)ov5640_ref_config[i][1];

    if (bsp_camera_write_reg(reg, val) != 0) {
      ov5640_ref_fail_index = (int32_t)i;
      ov5640_ref_fail_reg   = reg;
      return -1;
    }
    if (bsp_camera_read_reg(reg, &readback) != 0) {
      ov5640_ref_fail_index = (int32_t)i;
      ov5640_ref_fail_reg   = reg;
      return -1;
    }
    if (readback != val) {
      ov5640_ref_fail_index = (int32_t)i;
      ov5640_ref_fail_reg   = reg;
      ov5640_ref_fail_want  = val;
      ov5640_ref_fail_got   = readback;
      return -1;
    }
  }
  ov5640_ref_fail_index = -1;
  return 0;
}

/* 0x3808..0x380B must move as one atomic group, otherwise the sensor can latch
 * a half-updated width/height for a frame and emit a corrupt line count. */
static int32_t ov5640_ref_set_output_size(uint16_t width, uint16_t height)
{
  int32_t ret = 0;

  ret |= bsp_camera_write_reg(OV5640_GROUP_ACCESS, 0x03); /* open group 3 */
  ret |= bsp_camera_write_reg(OV5640_DVPHO_H, (uint8_t)(width >> 8));
  ret |= bsp_camera_write_reg(OV5640_DVPHO_L, (uint8_t)(width & 0xFFU));
  ret |= bsp_camera_write_reg(OV5640_DVPVO_H, (uint8_t)(height >> 8));
  ret |= bsp_camera_write_reg(OV5640_DVPVO_L, (uint8_t)(height & 0xFFU));
  ret |= bsp_camera_write_reg(OV5640_GROUP_ACCESS, 0x13); /* end group 3  */
  ret |= bsp_camera_write_reg(OV5640_GROUP_ACCESS, 0xa3); /* launch group */

  return ret;
}

int32_t ov5640_ref_init(void)
{
  /* Soft reset: clock from pad while the core is held, then reset. The 5 ms
   * settle is what the vendor sequence uses; shorter and the first SCCB write
   * after reset is silently dropped. */
  if (bsp_camera_write_reg(0x3103, 0x11) != 0) {
    return -1;
  }
  if (bsp_camera_write_reg(0x3008, 0x82) != 0) {
    return -1;
  }
  HAL_Delay(5);

  if (ov5640_ref_apply_table() != 0) {
    return -1;
  }
  if (ov5640_ref_set_output_size(OV5640_REF_OUT_W, OV5640_REF_OUT_H) != 0) {
    return -1;
  }
  return 0;
}
