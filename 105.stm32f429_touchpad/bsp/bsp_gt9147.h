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

/* Maximum number of simultaneous touch points the driver reports.  The GT9147
 * reports 5; the value actually in use comes from the configuration block
 * (0x804C) and is returned by bsp_gt9147_points(). */
#define GT9147_MAX_POINTS   5U

/* Bytes per point in the coordinate block.  A contact starts at 0x814F with
 * its track id, followed by x / y / size (16-bit little endian) and one
 * reserved byte:
 *   [0] track id   [1..2] x   [3..4] y   [5..6] size   [7] reserved
 * Note the id sits at 0x814F, one byte below the 0x8150 most examples quote:
 * reading from 0x8150 as if it started with the id shifts every field by one
 * byte and produces coordinates far outside the panel. */
#define GT9147_POINT_STRIDE 8U

/* Length of the Product ID string stored at GT_PID_REG (not zero terminated). */
#define GT9147_ID_LEN       4U

/* Touch resolution the shipped configuration block programs into the chip.
 * The panel is specified as 800x480 but the controller is configured for the
 * 480x800 GRAM window the NT35510 module is driven with. */
#define GT9147_PANEL_X      480U
#define GT9147_PANEL_Y      800U

typedef struct
{
    uint8_t  id;        /* track id (0..9): stable while that finger stays down,
                         * so the caller can tell "same finger moved" from
                         * "another finger appeared in this buffer slot"       */
    uint8_t  size;      /* reported contact area, 0..255 (pressure proxy)      */
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
  * @brief  Raw value of the touch status register (0x814E) from the most recent
  *         read.  Bit 7 = buffer ready, bits 3..0 = contact count.  Diagnostic
  *         only: it tells "no new frame" (0x00) apart from "all fingers up"
  *         (0x80) when the pointer does not move.
  */
uint8_t bsp_gt9147_last_status(void);

/**
  * @brief  Touch resolution the chip is currently configured for.  These come
  *         out of the configuration block (0x8048..0x804B) and are the ranges
  *         the raw coordinates in gt9147_point_t are expressed in.
  */
uint16_t bsp_gt9147_panel_x(void);
uint16_t bsp_gt9147_panel_y(void);

/**
  * @brief  Number of simultaneous contacts the chip is configured to report
  *         (configuration block 0x804C, capped at GT9147_MAX_POINTS).
  *         Read back from the chip during init, so it also proves whether the
  *         uploaded configuration really enabled multi-touch: a module left at
  *         the factory value 2 can never report three fingers.
  */
uint8_t bsp_gt9147_points(void);

/* Returned by bsp_gt9147_read() when the controller had nothing new to say.
 * This is NOT the same thing as a release: the GT9xx only sets bit 7 of
 * 0x814E once it has a fresh frame, and a poll that lands between two frames
 * reads a cleared status register.  Treating that as "finger lifted" cuts a
 * stroke into one-frame contacts and the cursor never moves. */
#define GT9147_NO_FRAME     (-2)

/**
  * @brief  Read the touch status register and, when a new frame is pending,
  *         the whole coordinate block in ONE burst.
  *
  *  The status register is only consumed when bit 7 (buffer ready) is set:
  *    - bit 7 set, count 0  -> the finger(s) really went up
  *    - bit 7 set, count n  -> a new coordinate frame
  *    - bit 7 clear         -> nothing new, register left untouched
  *
  *  All @p n points come from a single I2C transaction: the controller rewrites
  *  the complete block every report period, so reading the points one by one
  *  could mix two frames into one sample (a finger would appear to jump between
  *  two positions).  Each entry carries the track id, which is what makes
  *  multi-touch usable upstream.
  *
  *  @param points  destination array of at least GT9147_MAX_POINTS entries
  *  @param max     number of entries available in @p points
  *  @retval number of contacts written into @p points (0 = released),
  *          GT9147_NO_FRAME (-2) when no new frame is pending,
  *          or -1 when the chip is not initialised / not answering.
  */
int bsp_gt9147_read(gt9147_point_t *points, uint8_t max);

/**
  * @brief  Copy the coordinate block of the most recent frame exactly as it
  *         came off the bus (no parsing).  Diagnostic only: when the decoded
  *         coordinates look impossible, these bytes show whether the chip is
  *         reporting nonsense or the byte offset/order is wrong.
  *  @retval number of bytes copied (0 = no frame captured yet)
  */
uint8_t bsp_gt9147_last_block(uint8_t *buf, uint8_t max);

/**
  * @brief  Read the coordinate block at 0x814F (point 1 track id) right now,
  *         bypassing the frame gating.  Used by the console dump; 0 on success.
  */
int bsp_gt9147_dump_block(uint8_t *buf, uint8_t len);

/**
  * @brief  Enable/disable the per-frame raw trace on the console
  *         ("[GT9 ] n=1 raw: .. -> x= y= size=", rate limited to ~3 Hz).
  *
  *  Kept off in normal use: the trace is a bring-up tool and would otherwise
  *  interleave with the serial command replies whenever a finger is down.
  *  @param  on  0 = off, 1 = on, anything else = query only
  *  @retval the state after the call (0 = off)
  */
uint8_t bsp_gt9147_trace(uint8_t on);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_GT9147_H__ */
