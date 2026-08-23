/**
  ******************************************************************************
  * @file    bsp_eeprom_24c02.h
  * @brief   AT24C02 I2C EEPROM driver (256 B, 8 B/page) on I2C2 (PH4/PH5).
  *
  *          AT24C02 sits on the shared I2C2 bus with PCF8574 / AP3216C /
  *          MPU9250. The driver takes the shared bus mutex internally
  *          (web_i2c_lock / web_i2c_unlock) so every caller is protected
  *          against cross-task bus contention — callers do NOT need to lock.
  ******************************************************************************
  */
#ifndef __BSP_EEPROM_24C02_H
#define __BSP_EEPROM_24C02_H

#include <stdint.h>

/* AT24C02 on-bus address (A0/A1/A2 grounded):
 *   WRITE byte = 0xA0, READ byte = 0xA1.
 * IMPORTANT: the F4 HAL I2C master API does NOT shift the address. Its
 * I2C_7BIT_ADD_WRITE(a) is just (a & ~0x01) and I2C_7BIT_ADD_READ(a) is
 * (a | 0x01). So we pass the 8-bit WRITE address 0xA0 directly; the read
 * path derives 0xA1 via (addr | 0x01). Passing a 7-bit 0x50 would put 0x50
 * on the bus, which is the wrong slave address (NAK / AF). */
#define EEPROM_24C02_ADDR   0xA0U

#define EEPROM_24C02_SIZE   256U    /* 2 Kbit */
#define EEPROM_24C02_PAGE   8U      /* 8-byte page */
#define EEPROM_24C02_WR_MS  6U      /* max write cycle time (datasheet 5 ms) */

/**
  * @brief  Random-read one or more bytes starting at mem_addr.
  *         Thread-safe: takes the shared I2C bus mutex internally.
  * @retval 0 ok, -1 error (address out of range or I2C NAK)
  */
int EEPROM24_Read(uint16_t mem_addr, uint8_t *buf, uint16_t len);

/**
  * @brief  Write one or more bytes starting at mem_addr.
  *         Splits across page boundaries and inserts the required write-cycle
  *         delay after every page so the next sequencer is acknowledged.
  *         Thread-safe: takes the shared I2C bus mutex internally.
  * @retval 0 ok, -1 error
  */
int EEPROM24_Write(uint16_t mem_addr, const uint8_t *buf, uint16_t len);

#endif /* __BSP_EEPROM_24C02_H */
