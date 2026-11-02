/**
 * @file    bsp_delay.h
 * @brief   DWT cycle-counter busy-wait delay (HAL_Delay / vTaskDelay free).
 */
#ifndef __BSP_DELAY_H__
#define __BSP_DELAY_H__

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void bsp_delay_us(uint32_t us);
void bsp_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_DELAY_H__ */
