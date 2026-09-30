/**
 * @file bsp_sccb.h
 * @brief Bit-bang SCCB/I2C master for the camera module (Phase 3)
 *
 * The official Vision Board firmware talks to the camera over RT-Thread's
 * software I2C (drv_soft_i2c) on the same two pins, so a bit-banged master
 * with standard I2C timing is a proven match for these sensors.
 *
 * Pins (from the official projects + schematic):
 *   P1103 = SCCB_SCL, P50E = SCCB_SDA (both nets on the FPC_CAM_22P).
 */
#ifndef BSP_SCCB_H_
#define BSP_SCCB_H_

#include "bsp_api.h"

#define BSP_SCCB_SCL_PIN    BSP_IO_PORT_11_PIN_03   /* P1103 */
#define BSP_SCCB_SDA_PIN    BSP_IO_PORT_05_PIN_14   /* P50E  */

/** Configure both pins to bus idle (SCL output high, SDA released with pull-up). */
void bsp_sccb_init (void);

/** Probe a 7-bit address (START + address byte + expect ACK + STOP). */
bool bsp_sccb_probe (uint8_t addr7);

/* 16-bit register sensors (OV5640 class): 3-phase write, random read. */
fsp_err_t bsp_sccb_write16 (uint8_t addr7, uint16_t reg, uint8_t val);
fsp_err_t bsp_sccb_read16 (uint8_t addr7, uint16_t reg, uint8_t * p_val);

/* 8-bit register sensors (OV7725/GC class): kept for the scan/ID step. */
fsp_err_t bsp_sccb_write8 (uint8_t addr7, uint8_t reg, uint8_t val);
fsp_err_t bsp_sccb_read8 (uint8_t addr7, uint8_t reg, uint8_t * p_val);

#endif /* BSP_SCCB_H_ */
