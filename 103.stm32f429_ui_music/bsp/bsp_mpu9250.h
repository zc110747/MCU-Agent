/**
  ******************************************************************************
  * @file    bsp_mpu9250.h
  * @brief   MPU9250 9-axis IMU driver (I2C2): accel + gyro + AK8963 magnetometer.
  ******************************************************************************
  */
#ifndef __BSP_MPU9250_H__
#define __BSP_MPU9250_H__

#include <stdint.h>

typedef struct {
  float ax, ay, az;   /* acceleration, g        */
  float gx, gy, gz;   /* gyroscope, deg/s       */
  float mx, my, mz;   /* magnetometer, uT       */
} mpu9250_data_t;

/* Init MPU9250 + AK8963 (continuous mode).
 * Returns 0 on success, -1 WHO_AM_I mismatch, -2 AK8963 config write failed,
 * -3 AK8963 not detected (accel/gyro are still usable in that case). */
int bsp_mpu9250_init(void);

/* Read all 9 axes into *d. Returns 0 on success. */
int bsp_mpu9250_read(mpu9250_data_t *d);

/**
  * @brief  AK8963 device ID read during init: 0x48 when the magnetometer
  *         answers on the MPU9250's internal I2C master, 0 otherwise.
  *         Tells "no magnetometer fitted" (common on many modules) apart from
  *         "magnetometer read failed".
  */
uint8_t bsp_mpu9250_mag_id(void);

#endif /* __BSP_MPU9250_H__ */
