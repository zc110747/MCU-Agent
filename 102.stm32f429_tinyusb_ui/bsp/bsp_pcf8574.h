#ifndef __BSP_PCF8574_H
#define __BSP_PCF8574_H

#include "stm32f4xx_hal.h"

/* PCF8574 I/O expander (address A2=A1=A0=0 -> 0x20) */
#define PCF8574_ADDR  0x20U

/* PCF8574 pin assignment (see hardware manual):
 *   P7 = ETH_RESET (active-low through transistor)
 *   P1 = AP3216C INT
 *   P5 = MPU9250 INT
 *   P0 = BEEP (low = sound, confirmed by user: P0=L -> buzzer on)
 */
#define PCF8574_ETH_RESET_IO  7U   /* P7 controls ETH_RESET through a transistor */
#define PCF8574_BEEP_IO       0U   /* P0 drives the buzzer, LOW = sound */

HAL_StatusTypeDef BSP_PCF8574_Write(uint8_t data);
void BSP_ETH_PHY_Reset(void);

/* BEEP control (PCF8574 P0, LOW = sound) */
void BSP_BEEP_On(void);
void BSP_BEEP_Off(void);
void BSP_BEEP_Set(uint8_t on);

#endif /* __BSP_PCF8574_H */
