/**
  ******************************************************************************
  * @file    bsp_iic.h
  * @brief   Bit-banged I2C master on PH4 (SCL) / PH5 (SDA), for WM8978 only.
  *
  *          The WM8978 is the only I2C device in this project, its traffic is
  *          two bytes per control-register write, and it never needs clock
  *          stretching or multi-master arbitration.  A bit-banged bus is
  *          therefore preferred over HAL I2C: no peripheral state machine, no
  *          errata workarounds, and it keeps callers free to run before the
  *          FreeRTOS scheduler starts.
  *
  *          Timing: half period = BSP_IIC_DELAY_US, i.e. ~250 kHz at 2 us.  The
  *          WM8978 supports 400 kHz, so there is generous margin.
  ******************************************************************************
  */
#ifndef __BSP_IIC_H
#define __BSP_IIC_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_IIC_SCL_PORT    GPIOH
#define BSP_IIC_SCL_PIN     GPIO_PIN_4
#define BSP_IIC_SDA_PORT    GPIOH
#define BSP_IIC_SDA_PIN     GPIO_PIN_5

#define BSP_IIC_DELAY_US    2U

/** Configure PH4/PH5 as open-drain-capable push-pull outputs, bus idle (high). */
void bsp_iic_init(void);

/**
  * @brief  Write one or more bytes in a single transaction (START .. STOP).
  *
  *         The WM8978 register protocol is a two-byte payload per write
  *         ({reg<<1 | msb, lsb}), which a single-byte helper cannot express.
  *
  * @retval true  every byte was acknowledged
  * @retval false NACK or timeout (bus released before returning)
  */
bool bsp_iic_write_bytes(uint8_t dev_addr, const uint8_t *data, uint8_t len);

/**
  * @brief  Write one byte and report whether the slave acknowledged.
  * @retval true  ACK received
  * @retval false NACK or timeout (bus released before returning)
  */
bool bsp_iic_write_byte(uint8_t dev_addr, uint8_t value);

/**
  * @brief  Read one byte without a register pointer (WM8978 is write-only over
  *         this bus, so this exists for bus scanning only).
  */
uint8_t bsp_iic_read_byte(uint8_t dev_addr);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_IIC_H */
