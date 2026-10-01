/**
 * @file bsp_touch.h
 * @brief CST812T capacitive touch panel (SCI3 I2C) for RA8D1 Vision Board
 *
 * The fitted panel is the MIPI DSI 2.0" (480x360) unit, which carries a
 * Hynitron CST812T controller.  Unlike the reference RT-Thread BSP (which
 * pulls in the RT-Thread i2c-device + touch-device frameworks and their
 * pin-number getters) this driver talks to the chip straight through the FSP
 * I2C master API and exposes one small polling read - which is all an LVGL
 * pointer indev needs.
 *
 * Bus wiring (traced on Vision_Board_schematic.pdf, DISPLAY CTRL block):
 *   SCL = P408, SDA = P409   -> IOPORT_PERIPHERAL_SCI1_3_5_7_9 + NMOS, SCI3
 *   RST = P000 (active low), INT = P010 (falling edge, active low)
 *   I2C 7-bit slave address 0x15, 100 kHz
 *
 * NOTE the pins: P208/P209 are the SCI9 debug console, not the touch bus.
 * The panel nets CTP_SCL/CTP_SDA map to MCU nets SCL0/SDA0, which are P408
 * and P409.  Both pairs share the same IOPORT PSEL group value, which is the
 * trap that makes them look interchangeable.
 *
 * The chip has no register address space in the usual sense: the 8-bit
 * register index is written, then the read follows a repeated START.  A full
 * touch sample is one 5-byte burst starting at register 0x02:
 *   [0] finger number (low nibble)  [1] x[11:8]  [2] x[7:0]
 *   [3] y[11:8]                     [4] y[7:0]
 */
#ifndef BSP_TOUCH_H_
#define BSP_TOUCH_H_

#include "bsp_api.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One sampled touch state. */
typedef struct
{
    bool     pressed;   /**< true while a finger is down                      */
    uint16_t x;         /**< panel X in pixels (0 .. BSP_TOUCH_WIDTH-1)        */
    uint16_t y;         /**< panel Y in pixels (0 .. BSP_TOUCH_HEIGHT-1)       */
} bsp_touch_point_t;

/* Panel-native touch resolution, pre-defined by the CST812T firmware. */
#define BSP_TOUCH_WIDTH      (480U)
#define BSP_TOUCH_HEIGHT     (360U)

/* Panel control / interrupt pins. */
#define BSP_TOUCH_PIN_RST    BSP_IO_PORT_00_PIN_00
#define BSP_TOUCH_PIN_INT    BSP_IO_PORT_00_PIN_10

/* I2C bus pins (SCI3 simple-I2C).  Exposed so the driver can flip them to
   plain GPIO to check that the bus is not physically stuck before blaming a
   NACK on the slave. */
#define BSP_TOUCH_PIN_SCL    BSP_IO_PORT_04_PIN_08
#define BSP_TOUCH_PIN_SDA    BSP_IO_PORT_04_PIN_09

/**
 * Open the SCI3 I2C master, release the controller from reset and latch its
 * address.  Safe to call more than once (subsequent calls are a no-op).
 *
 * @return FSP_SUCCESS when the bus opened; FSP_ERR_* otherwise.
 */
fsp_err_t bsp_touch_init (void);

/**
 * Read the current finger state.  Non-blocking from the caller's point of
 * view (one short I2C burst plus restart wait).
 *
 * @param[out] p_point  Destination; untouched when the transfer fails.
 * @return FSP_SUCCESS on a completed I2C burst (pressed reflects the finger
 *         count), FSP_ERR_* when the controller did not answer.
 */
fsp_err_t bsp_touch_read (bsp_touch_point_t * p_point);

/** True once bsp_touch_init() has completed. */
bool bsp_touch_ready (void);

/** Firmware version byte read at init (0 when unavailable). */
uint8_t bsp_touch_fw_version (void);

/**
 * Diagnostic: probe one 7-bit I2C address and report whether it ACKed.
 * Used by the 'touch scan' console command when the chip does not answer at
 * the expected address - a NACK and a wrong address look identical otherwise.
 *
 * @param[in]  addr  7-bit slave address to probe.
 * @return FSP_SUCCESS when the slave ACKed, FSP_ERR_* otherwise.
 */
fsp_err_t bsp_touch_probe_addr (uint8_t addr);

/**
 * Diagnostic: read the firmware-version register.
 *
 * @param[out] buf  Destination, at most 1 byte is written.
 * @param[in]  len  Number of bytes to read (clamped to 1).
 * @return FSP_SUCCESS when the read completed.
 */
fsp_err_t bsp_touch_product_id (uint8_t * buf, uint32_t len);

/** Diagnostic: raw finger-count register byte (0xFF when the read failed). */
uint8_t bsp_touch_status_raw (void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TOUCH_H_ */
