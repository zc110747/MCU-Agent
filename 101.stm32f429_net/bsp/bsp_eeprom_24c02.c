/**
  ******************************************************************************
  * @file    bsp_eeprom_24c02.c
  * @brief   AT24C02 I2C EEPROM driver implementation (polling mode).
  *
  *          Uses the I2C2 handle (hi2c2) shared with PCF8574 / AP3216C /
  *          MPU9250. The driver uses HAL's *polling* API
  *          (HAL_I2C_Master_Transmit/Receive) instead of HAL_I2C_Mem_Read/
  *          Write, because this project does NOT enable the I2C2 peripheral/
  *          event interrupts in NVIC (PCF8574 talks to the bus in polling
  *          mode too). HAL_I2C_Mem_Read/Write are interrupt-driven and would
  *          time out forever without the NVIC lines enabled.
  *
  *          The I2C2 bus is shared across tasks (web httpd/httpsd reading
  *          sensors, PCF8574 controlling ETH_RESET, and netcfg saving to
  *          EEPROM). To prevent cross-task bus contention the driver takes
  *          the shared bus mutex (web_i2c_lock / web_i2c_unlock) around every
  *          transfer, so callers never need to lock manually.
  ******************************************************************************
  */
#include "bsp_eeprom_24c02.h"
#include "bsp_i2c.h"
#include "bsp_delay.h"
#include "web_serve.h"   /* web_i2c_lock / web_i2c_unlock (shared I2C bus) */

#include "stm32f4xx_hal.h"

/* EEPROM_24C02_ADDR is the 8-bit WRITE address 0xA0. The F4 HAL I2C master
 * API does NOT shift the address: I2C_7BIT_ADD_WRITE(a) = (a & ~1) and
 * I2C_7BIT_ADD_READ(a) = (a | 1). So passing 0xA0 yields the on-bus write
 * byte 0xA0, and (0xA0 | 1) = 0xA1 yields the read byte. This is correct. */
#define EEPROM_DEV_7BIT   ((uint16_t)EEPROM_24C02_ADDR)

int EEPROM24_Read(uint16_t mem_addr, uint8_t *buf, uint16_t len)
{
  if (buf == NULL || len == 0) return -1;
  if ((uint32_t)mem_addr + len > EEPROM_24C02_SIZE) return -1;

  web_i2c_lock();

  /* Phase 1: send the 1-byte memory address we want to read from. */
  uint8_t addr = (uint8_t)(mem_addr & 0xFFU);
  HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c2, EEPROM_DEV_7BIT,
                                                 &addr, 1U, 100U);
  if (st != HAL_OK) { web_i2c_unlock(); return -1; }

  /* Phase 2: repeated-start read of len bytes. */
  st = HAL_I2C_Master_Receive(&hi2c2, (uint16_t)(EEPROM_DEV_7BIT | 0x01U),
                              buf, len, 100U);

  web_i2c_unlock();
  return (st == HAL_OK) ? 0 : -1;
}

int EEPROM24_Write(uint16_t mem_addr, const uint8_t *buf, uint16_t len)
{
  if (buf == NULL || len == 0) return -1;
  if ((uint32_t)mem_addr + len > EEPROM_24C02_SIZE) return -1;

  web_i2c_lock();

  uint16_t off = 0;
  while (off < len)
  {
    /* Bytes remaining in the current 8-byte page. Writing past the page edge
     * would wrap inside the page and corrupt other data, so we split. */
    uint16_t page_rem = (uint16_t)(EEPROM_24C02_PAGE -
                                   (mem_addr & (EEPROM_24C02_PAGE - 1U)));
    uint16_t chunk = (uint16_t)(len - off);
    if (chunk > page_rem) chunk = page_rem;

    /* 24C02 write format: [DEV][mem_addr][data...]. Build a tx buffer that
     * leads with the 1-byte memory address, then the payload. */
    uint8_t tx[1U + EEPROM_24C02_PAGE];
    tx[0] = (uint8_t)(mem_addr & 0xFFU);
    for (uint16_t i = 0; i < chunk; i++)
      tx[1U + i] = buf[off + i];

    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c2, EEPROM_DEV_7BIT,
                                                   tx, (uint16_t)(1U + chunk),
                                                   100U);
    if (st != HAL_OK) { web_i2c_unlock(); return -1; }

    /* Wait for the internal write cycle to finish before the next access. */
    bsp_delay_ms(EEPROM_24C02_WR_MS);

    mem_addr = (uint16_t)(mem_addr + chunk);
    off = (uint16_t)(off + chunk);
  }

  web_i2c_unlock();
  return 0;
}
