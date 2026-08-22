#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm32f4xx_hal.h"

/* Debug console on USART1: PA9(TX) / PA10(RX), 115200 8N1 */
extern UART_HandleTypeDef huart1;

void BSP_UART_Init(void);

#endif /* __BSP_UART_H */
