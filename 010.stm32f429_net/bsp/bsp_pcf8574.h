#ifndef __BSP_PCF8574_H
#define __BSP_PCF8574_H

#include "stm32f4xx_hal.h"

/* PCF8574 I/O expander (address A2=A1=A0=0 -> 0x20) */
#define PCF8574_ADDR  0x20U

/* PCF8574 pin assignment (see hardware manual) */
#define PCF8574_ETH_RESET_IO  7U   /* P7 controls ETH_RESET through a transistor */

HAL_StatusTypeDef BSP_PCF8574_Write(uint8_t data);
void BSP_ETH_PHY_Reset(void);

#endif /* __BSP_PCF8574_H */
