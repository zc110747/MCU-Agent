#ifndef __BSP_I2C_H
#define __BSP_I2C_H

#include "stm32f4xx_hal.h"

/* I2C2 on PH4(SCL) / PH5(SDA), used to talk to the PCF8574 I/O expander */
extern I2C_HandleTypeDef hi2c2;

void BSP_I2C_Init(void);

/**
  * @brief  Recover a stuck I2C bus.
  *
  *   STM32 I2C + HAL has a well-known failure mode: if a slave holds SDA low
  *   (electrical noise, a glitch during clock stretching, or a reset that hits
  *   the bus mid-transaction), HAL_I2C_Mem_Read() keeps timing out and the bus
  *   never recovers on its own. Every later read fails, so a periodic collector
  *   (hwinfo_task) publishes all-zero sensor values forever.
  *
  *   This routine detects that condition and forces a recovery:
  *     1. If SDA is stuck LOW (or the peripheral reports BUSY / AF), reconfigure
  *        SCL as a push-pull GPIO output and toggle it >=9 times. A compliant
  *        I2C slave releases SDA after it has been clocked out of its current
  *        byte, freeing the bus.
  *     2. De-init then re-init the I2C peripheral to clear any latched error
  *        flags (BUSY / AF / ARLO / BERR).
  *
  *   @retval 0  bus was healthy or recovered successfully
  *   @retval -1 recovery failed (hardware still stuck)
  */
int BSP_I2C_Recover(void);

#endif /* __BSP_I2C_H */
