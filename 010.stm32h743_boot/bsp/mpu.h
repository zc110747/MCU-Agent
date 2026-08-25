/**
  ******************************************************************************
  * @file    bsp/mpu.h
  * @brief   MPU configuration - keeps all bootloader-visible memory
  *          non-cacheable so flash writes / QSPI / USB DMA never see
  *          stale cache lines.
  ******************************************************************************
  */
#ifndef __BSP_MPU_H
#define __BSP_MPU_H

#include "stm32h7xx_hal.h"

void BSP_MPU_Init(void);

#endif /* __BSP_MPU_H */
