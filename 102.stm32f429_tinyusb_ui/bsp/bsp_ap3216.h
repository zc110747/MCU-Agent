/**
  ******************************************************************************
  * @file    bsp_ap3216.h
  * @brief   AP3216C ambient light + proximity + IR sensor driver (I2C2).
  ******************************************************************************
  */
#ifndef __BSP_AP3216_H__
#define __BSP_AP3216_H__

#include <stdint.h>

typedef struct {
  uint16_t lux;   /* ambient light (ALS) */
  uint16_t ps;    /* proximity */
  uint16_t ir;    /* infrared */
} ap3216c_data_t;

/* Init the sensor (ALS+PS+IR mode). Returns 0 on success. */
int bsp_ap3216c_init(void);

/* Read all three channels into *d. Returns 0 on success. */
int bsp_ap3216c_read(ap3216c_data_t *d);

#endif /* __BSP_AP3216_H__ */
