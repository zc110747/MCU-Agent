/**
  ******************************************************************************
  * @file    sensor_task.h
  * @brief   Periodic sampler for the AP3216C and the MPU9250 on I2C2.
  *
  *  The sampled values are published in a single struct that the UI thread
  *  copies out with sensor_get().  Keeping the I2C traffic in its own thread
  *  means a slow magnetometer read never stalls the LVGL render pump.
  ******************************************************************************
  */
#ifndef __SENSOR_TASK_H__
#define __SENSOR_TASK_H__

#include <stdint.h>

/* Stack size in words.  The MPU9250 read is ~14 I2C transactions deep. */
#define SENSOR_TASK_STACK_WORDS   512U

/* Lowest priority: the panel must stay responsive. */
#define SENSOR_TASK_PRIO          (tskIDLE_PRIORITY + 1U)

/* Sampling period.  The MPU9250 read takes ~15 ms, so 500 ms is ample. */
#define SENSOR_SAMPLE_MS          500U

typedef struct
{
    uint8_t  ap3216_ok;     /* 1 when the last AP3216C read succeeded   */
    uint16_t ir;            /* infrared                                  */
    uint16_t als;           /* ambient light, lux                        */
    uint16_t ps;            /* proximity                                 */

    uint8_t  mpu_ok;        /* 1 when the last accel/gyro read succeeded    */
    uint8_t  mag_ok;        /* 1 when the last AK8963 read succeeded        */
    float    ax, ay, az;    /* acceleration, g                           */
    float    gx, gy, gz;    /* angular rate, deg/s                       */
    float    mx, my, mz;    /* magnetic field, uT                        */

    uint32_t samples;       /* successful sampling rounds                */
    uint32_t errors;        /* I2C failures since boot                    */
} sensor_data_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
  * @brief  Sensor sampler thread body.
  */
void sensor_task(void *arg);

/**
  * @brief  Copy the latest sample out.  Safe to call from any task.
  */
void sensor_get(sensor_data_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_TASK_H__ */
