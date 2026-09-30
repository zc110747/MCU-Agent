/**
 * @file bsp_ov5640.c
 * @brief Minimal OV5640 driver (QVGA RGB565 + colorbar)
 *
 * Window derivation for QVGA (w=320, h=240) from the official
 * set_framesize(), with constants from ov5640.c (SENSOR 2624x1964,
 * ACTIVE 2592x1944, DUMMY_WIDTH_BUFFER 16, DUMMY_HEIGHT_BUFFER 8,
 * DUMMY_COLUMNS 16, DUMMY_LINES 6, HSYNC_TIME 252, VYSNC_TIME 24):
 *
 *   readout           = 2592 x 1944 (full array after reset)
 *   sensor_div        = 2            (320 <= 2592/2 -> subsample x4)
 *   sensor_w/h        = 2592+16 = 2608 / 1944+8 = 1952
 *   sensor_ws         = clamp((2592-2608)/4*2, -8) + 16 = -8 + 16 = 8
 *   sensor_we         = 8 + 2608 - 1 = 2615
 *   sensor_hs         = clamp((1944-1952)/4*2, -4) + 6 = -4 + 6 = 2
 *   sensor_he         = 2 + 1952 - 1 = 1953
 *   ratio             = min(1296/320, 972/240) = 4 (integer)
 *   x_off             = (2608/2 - 320*4)/2 = 12
 *   y_off             = (1952/2 - 240*4)/2 = 8
 *   hts_target        = 2608/2 = 1304
 *   sensor_hts        = 1304 + 160 (<=640 fix) + 252 = 1716
 *   sensor_vts        = max(1952/2 + 24, (1964+24)/8) = max(1000, 248) = 1000
 *   x_inc / y_inc     = ((2*2-1) << 4) | 1 = 0x31
 *   0x3820/0x3821     = bit0 set (div > 1)
 */
#include "bsp_ov5640.h"

#include "bsp_pin.h"
#include "bsp_sccb.h"
#include "r_ioport.h"

#define CAM_PWDN_PIN        BSP_IO_PORT_07_PIN_05   /* P705 = VIO_PWDN (schematic) */
#define CAM_RESET_PIN       BSP_IO_PORT_07_PIN_04   /* P704 = VIO_RESET (schematic) */

/* OV5640 register addresses (official ov5640_regs.h names). */
#define REG_SYSTEM_CTROL0   0x3008U  /* bit7 sw reset, bit6 sw standby */
#define REG_SCCB_SYS_CTRL1  0x3103U
#define REG_SYS_RESET_02    0x3002U
#define REG_CLOCK_ENABLE_02 0x3006U
#define REG_PID_H           0x300AU
#define REG_PID_L           0x300BU
#define REG_TIMING_HS_H     0x3800U
#define REG_TIMING_HS_L     0x3801U
#define REG_TIMING_VS_H     0x3802U
#define REG_TIMING_VS_L     0x3803U
#define REG_TIMING_HW_H     0x3804U
#define REG_TIMING_HW_L     0x3805U
#define REG_TIMING_VH_H     0x3806U
#define REG_TIMING_VH_L     0x3807U
#define REG_TIMING_DVPHO_H  0x3808U
#define REG_TIMING_DVPHO_L  0x3809U
#define REG_TIMING_DVPVO_H  0x380AU
#define REG_TIMING_DVPVO_L  0x380BU
#define REG_TIMING_HTS_H    0x380CU
#define REG_TIMING_HTS_L    0x380DU
#define REG_TIMING_VTS_H    0x380EU
#define REG_TIMING_VTS_L    0x380FU
#define REG_TIMING_HOFF_H   0x3810U
#define REG_TIMING_HOFF_L   0x3811U
#define REG_TIMING_VOFF_H   0x3812U
#define REG_TIMING_VOFF_L   0x3813U
#define REG_TIMING_X_INC    0x3814U
#define REG_TIMING_Y_INC    0x3815U
#define REG_TIMING_TC_20    0x3820U
#define REG_TIMING_TC_21    0x3821U
#define REG_FORMAT_CONTROL  0x4300U
#define REG_VFIFO_HSIZE_H   0x4602U
#define REG_VFIFO_HSIZE_L   0x4603U
#define REG_VFIFO_VSIZE_H   0x4604U
#define REG_VFIFO_VSIZE_L   0x4605U
#define REG_FORMAT_CTRL_MUX 0x501FU
#define REG_PRE_ISP_TEST    0x503DU

/* Verbatim power-on table from the official OpenMV-derived driver
   (single-include data header, static storage, ends with {0,0,0}). */
#include "bsp_ov5640_default_regs.h"

void bsp_ov5640_power_levels (bool pwdn_high, bool reset_high)
{
    (void) bsp_pin_cfg(CAM_PWDN_PIN,
                       (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                       (pwdn_high ? (uint32_t) IOPORT_CFG_PORT_OUTPUT_HIGH :
                                    (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW));
    (void) bsp_pin_cfg(CAM_RESET_PIN,
                       (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                       (reset_high ? (uint32_t) IOPORT_CFG_PORT_OUTPUT_HIGH :
                                     (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW));
    R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
}

void bsp_ov5640_power_up (void)
{
    /* OV5640: PWDN active high, RESET active low. Both idle = running. */
    bsp_ov5640_power_levels(false, false);                  /* in reset  */
    (void) bsp_pin_write(CAM_RESET_PIN, BSP_IO_LEVEL_HIGH); /* out of reset */
    R_BSP_SoftwareDelay(10, BSP_DELAY_UNITS_MILLISECONDS);
}

fsp_err_t bsp_ov5640_read_id (uint8_t addr7, uint16_t * p_id)
{
    uint8_t hi = 0U;
    uint8_t lo = 0U;
    fsp_err_t err = bsp_sccb_read16(addr7, REG_PID_H, &hi);

    if (FSP_SUCCESS == err)
    {
        err = bsp_sccb_read16(addr7, REG_PID_L, &lo);
    }

    if (FSP_SUCCESS == err)
    {
        *p_id = (uint16_t) (((uint16_t) hi << 8) | lo);
    }

    return err;
}

/** set_pixformat(PIXFORMAT_RGB565) from the official driver. */
static fsp_err_t ov5640_set_rgb565 (uint8_t addr7)
{
    uint8_t reg = 0U;
    fsp_err_t err = FSP_SUCCESS;

    err |= bsp_sccb_write16(addr7, REG_FORMAT_CONTROL, 0x6F);
    err |= bsp_sccb_write16(addr7, REG_FORMAT_CTRL_MUX, 0x01);

    err |= bsp_sccb_read16(addr7, REG_TIMING_TC_21, &reg);
    err |= bsp_sccb_write16(addr7, REG_TIMING_TC_21, (uint8_t) (reg & 0xDFU)); /* clear JPEG bit */

    err |= bsp_sccb_read16(addr7, REG_SYS_RESET_02, &reg);
    err |= bsp_sccb_write16(addr7, REG_SYS_RESET_02, (uint8_t) ((reg & 0xE3U) | 0x1CU));

    err |= bsp_sccb_read16(addr7, REG_CLOCK_ENABLE_02, &reg);
    err |= bsp_sccb_write16(addr7, REG_CLOCK_ENABLE_02, (uint8_t) (reg & 0xD7U));

    return err;
}

/** set_framesize(FRAMESIZE_QVGA) with the pre-computed window (see header). */
static fsp_err_t ov5640_set_qvga_window (uint8_t addr7)
{
    uint8_t reg = 0U;
    fsp_err_t err = FSP_SUCCESS;

    err |= bsp_sccb_write16(addr7, REG_TIMING_HS_H, 0x00);
    err |= bsp_sccb_write16(addr7, REG_TIMING_HS_L, 0x08);   /* ws  = 8    */
    err |= bsp_sccb_write16(addr7, REG_TIMING_VS_H, 0x00);
    err |= bsp_sccb_write16(addr7, REG_TIMING_VS_L, 0x02);   /* hs  = 2    */
    err |= bsp_sccb_write16(addr7, REG_TIMING_HW_H, 0x0A);
    err |= bsp_sccb_write16(addr7, REG_TIMING_HW_L, 0x37);   /* we  = 2615 */
    err |= bsp_sccb_write16(addr7, REG_TIMING_VH_H, 0x07);
    err |= bsp_sccb_write16(addr7, REG_TIMING_VH_L, 0xA1);   /* he  = 1953 */
    err |= bsp_sccb_write16(addr7, REG_TIMING_DVPHO_H, 0x01);
    err |= bsp_sccb_write16(addr7, REG_TIMING_DVPHO_L, 0x40);/* 320 */
    err |= bsp_sccb_write16(addr7, REG_TIMING_DVPVO_H, 0x00);
    err |= bsp_sccb_write16(addr7, REG_TIMING_DVPVO_L, 0xF0);/* 240 */
    err |= bsp_sccb_write16(addr7, REG_TIMING_HTS_H, 0x06);
    err |= bsp_sccb_write16(addr7, REG_TIMING_HTS_L, 0xB4);  /* hts = 1716 */
    err |= bsp_sccb_write16(addr7, REG_TIMING_VTS_H, 0x03);
    err |= bsp_sccb_write16(addr7, REG_TIMING_VTS_L, 0xE8);  /* vts = 1000 */
    err |= bsp_sccb_write16(addr7, REG_TIMING_HOFF_H, 0x00);
    err |= bsp_sccb_write16(addr7, REG_TIMING_HOFF_L, 0x0C); /* x_off = 12 */
    err |= bsp_sccb_write16(addr7, REG_TIMING_VOFF_H, 0x00);
    err |= bsp_sccb_write16(addr7, REG_TIMING_VOFF_L, 0x08); /* y_off = 8  */
    err |= bsp_sccb_write16(addr7, REG_TIMING_X_INC, 0x31);  /* div = 2    */
    err |= bsp_sccb_write16(addr7, REG_TIMING_Y_INC, 0x31);

    err |= bsp_sccb_read16(addr7, REG_TIMING_TC_20, &reg);
    err |= bsp_sccb_write16(addr7, REG_TIMING_TC_20, (uint8_t) (reg | 0x01U));
    err |= bsp_sccb_read16(addr7, REG_TIMING_TC_21, &reg);
    err |= bsp_sccb_write16(addr7, REG_TIMING_TC_21, (uint8_t) (reg | 0x01U));

    err |= bsp_sccb_write16(addr7, REG_VFIFO_HSIZE_H, 0x01);
    err |= bsp_sccb_write16(addr7, REG_VFIFO_HSIZE_L, 0x40);
    err |= bsp_sccb_write16(addr7, REG_VFIFO_VSIZE_H, 0x00);
    err |= bsp_sccb_write16(addr7, REG_VFIFO_VSIZE_L, 0xF0);

    return err;
}

fsp_err_t bsp_ov5640_init_qvga_rgb565 (uint8_t addr7)
{
    fsp_err_t err = FSP_SUCCESS;

    /* reset(): sw reset, then the verbatim default_regs table. */
    err |= bsp_sccb_write16(addr7, REG_SCCB_SYS_CTRL1, 0x11);
    err |= bsp_sccb_write16(addr7, REG_SYSTEM_CTROL0, 0x82);
    R_BSP_SoftwareDelay(5, BSP_DELAY_UNITS_MILLISECONDS);

    for (uint32_t i = 0U; 0U != ov5640_default_regs[i][0]; i++)
    {
        uint16_t reg = (uint16_t) (((uint16_t) ov5640_default_regs[i][0] << 8) |
                                   ov5640_default_regs[i][1]);
        err |= bsp_sccb_write16(addr7, reg, ov5640_default_regs[i][2]);
    }

    R_BSP_SoftwareDelay(300, BSP_DELAY_UNITS_MILLISECONDS);

    err |= ov5640_set_rgb565(addr7);
    err |= ov5640_set_qvga_window(addr7);

    return err;
}

fsp_err_t bsp_ov5640_set_colorbar (uint8_t addr7, bool enable)
{
    uint8_t reg = 0U;
    fsp_err_t err = bsp_sccb_read16(addr7, REG_PRE_ISP_TEST, &reg);

    if (FSP_SUCCESS == err)
    {
        err = bsp_sccb_write16(addr7, REG_PRE_ISP_TEST,
                               (uint8_t) ((reg & 0x7FU) | (enable ? 0x80U : 0x00U)));
    }

    return err;
}
