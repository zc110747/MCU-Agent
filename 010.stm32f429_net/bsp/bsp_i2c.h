#ifndef __BSP_I2C_H
#define __BSP_I2C_H

#include "stm32f4xx_hal.h"

/* I2C2 on PH4(SCL) / PH5(SDA), used to talk to the PCF8574 I/O expander */
extern I2C_HandleTypeDef hi2c2;

void BSP_I2C_Init(void);

#endif /* __BSP_I2C_H */
