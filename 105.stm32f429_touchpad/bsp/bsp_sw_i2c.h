/**
  ******************************************************************************
  * @file    bsp_sw_i2c.h
  * @brief   Software (bit-bang) I2C master used by the capacitive touch chip.
  *
  *  WHY THIS IS I2C AND NOT SPI
  *  ---------------------------
  *  The board silk prints the touch header as a SPI port:
  *
  *      T_CS  = PI8      T_SCK  = PH6      T_MOSI = PI3
  *      T_MISO = PG3     T_PEN  = PH7
  *
  *  The device actually soldered to the panel is a GT9147, and the GT9147 has
  *  an I2C interface only - there is no SPI mode in its datasheet.  On this
  *  board the controller's serial bus lands on the two pins the header calls
  *  T_SCK / T_MOSI:
  *
  *      T_SCK  (PH6) -> CT_SCL   GT9147 serial clock
  *      T_MOSI (PI3) -> CT_SDA   GT9147 serial data (bidirectional)
  *      T_CS   (PI8) -> CT_RST   GT9147 reset
  *      T_PEN  (PH7) -> CT_INT   GT9147 interrupt / touch indication
  *      T_MISO (PG3) -> unused by the capacitive panel
  *
  *  So this module is a software I2C master (open-drain, ~165 kHz) that drives
  *  those two pins.  SDA is written and read back on the same pin; no direction
  *  switching is needed because the pin is a true open-drain output with a
  *  pull-up, and reading the input data register works while the output
  *  driver is configured.
  ******************************************************************************
  */
#ifndef __BSP_SW_I2C_H__
#define __BSP_SW_I2C_H__

#include <stdint.h>

/* Half period of the serial clock.  3 us per half period -> ~165 kHz, inside
 * the 100..400 kHz I2C window and slow enough to be immune to the bus
 * contention caused by the FMC (LCD + SDRAM) traffic. */
#define SWI2C_HALF_PERIOD_US    3U

/* Number of clock pulses issued by bsp_sw_i2c_bus_reset() when SDA is found
 * stuck low (a slave holding the bus mid-byte). */
#define SWI2C_RESET_PULSES      9U

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Configure SCL/SDA as open-drain outputs with pull-ups, bus idle.
  */
void bsp_sw_i2c_init(void);

/**
  * @brief  Generate a START condition (SDA falls while SCL is high).
  */
void bsp_sw_i2c_start(void);

/**
  * @brief  Generate a STOP condition (SDA rises while SCL is high).
  */
void bsp_sw_i2c_stop(void);

/**
  * @brief  Clock one byte out, MSB first, and read the slave's ACK bit.
  * @retval 0 the slave acknowledged, -1 the slave did not answer (NACK).
  */
int bsp_sw_i2c_send_byte(uint8_t data);

/**
  * @brief  Clock one byte in, MSB first.
  * @param  ack 1 to acknowledge (more bytes follow), 0 to NACK (last byte).
  */
uint8_t bsp_sw_i2c_read_byte(int ack);

/**
  * @brief  Probe a 7-bit address with a write transfer.
  * @retval 0 a device answered at that address, -1 nothing there.
  */
int bsp_sw_i2c_probe(uint8_t addr7);

/**
  * @brief  Toggle SCL until SDA is released by a slave stuck mid-transfer.
  * @retval 0 SDA is free, -1 SDA is still held low (hardware fault).
  */
int bsp_sw_i2c_bus_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_SW_I2C_H__ */
