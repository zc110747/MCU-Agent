/**
  ******************************************************************************
  * @file    bsp_gt9147.h
  * @brief   GT9147 capacitive touch controller driver (software I2C).
  *
  *  The register map, the reset/address-latch sequence and the 184-byte
  *  configuration block follow the GT9147 programming guide.  The bus itself
  *  is the bit-banged I2C in bsp_sw_i2c.c (PH6 = SCL, PI3 = SDA).
  *
  *  Address selection
  *  -----------------
  *  GT9147 latches its slave address from the INT pin while RST is released:
  *      INT high during reset -> 0x14 (write 0x28 / read 0x29)
  *      INT low  during reset -> 0x5D (write 0xBA / read 0xBB)
  *  bsp_gt9147_init() tries 0x14 first, then 0x5D, and reports which one
  *  answered together with the product ID it read - that is the "is the
  *  accessed ID the expected one" check.
  ******************************************************************************
  */
#ifndef __BSP_GT9147_H__
#define __BSP_GT9147_H__

#include <stdint.h>

/* Maximum number of simultaneous touch points the driver reports. */
#define GT9147_MAX_POINTS   5U

/* Length of the Product ID string stored at GT_PID_REG (not zero terminated). */
#define GT9147_ID_LEN       4U

/* Touch resolution the shipped configuration block programs into the chip.
 * The panel is specified as 800x480 but the controller is configured for the
 * 480x800 GRAM window the NT35510 module is driven with. */
#define GT9147_PANEL_X      480U
#define GT9147_PANEL_Y      800U

typedef struct
{
    uint16_t x;         /* raw touch X (0 .. GT9147_PANEL_X-1) */
    uint16_t y;         /* raw touch Y (0 .. GT9147_PANEL_Y-1) */
} gt9147_point_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Reset the controller, latch its I2C address, read and verify the
  *         product ID and refresh the configuration if the stored version is
  *         older than the one shipped with this driver.
  *
  *         Prints the outcome of every step, including the product ID and
  *         whether it matches a known GT9xx part.
  *
  *  @retval 0  a GT9xx answered and is configured
  *  @retval -1 no device answered on either I2C address
  *  @retval -2 a device answered but the product ID is unknown
  */
int bsp_gt9147_init(void);

/**
  * @brief  @retval 1 init succeeded and the chip can be polled, 0 otherwise.
  */
int bsp_gt9147_is_ready(void);

/**
  * @brief  Product ID read during init ("9147", "911", ...), always a valid
  *         zero-terminated string; "?" when no chip was found.
  */
const char *bsp_gt9147_id(void);

/**
  * @brief  I2C address (7-bit, unshifted) the chip answered on: 0x14 or 0x5D.
  */
uint8_t bsp_gt9147_addr(void);

/**
  * @brief  Version byte of the configuration currently stored in the chip.
  */
uint8_t bsp_gt9147_cfg_version(void);

/**
  * @brief  Touch resolution the chip is currently configured for.  These come
  *         out of the configuration block (0x8048..0x804B) and are the ranges
  *         the raw coordinates in gt9147_point_t are expressed in.
  */
uint16_t bsp_gt9147_panel_x(void);
uint16_t bsp_gt9147_panel_y(void);

/**
  * @brief  Read the touch status register and, when contacts are present, all
  *         reported coordinates.  Clears the status register afterwards so the
  *         chip can raise the next interrupt.
  *
  *  @param points  destination array of at least GT9147_MAX_POINTS entries
  *  @param max     number of entries available in @p points
  *  @retval number of contacts written into @p points (0 = no touch),
  *          or -1 when the chip is not initialised / not answering.
  */
int bsp_gt9147_read(gt9147_point_t *points, uint8_t max);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_GT9147_H__ */
