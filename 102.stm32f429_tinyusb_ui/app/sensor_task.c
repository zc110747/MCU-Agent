/**
  ******************************************************************************
  * @file    sensor_task.c
  * @brief   Periodic sampler for the AP3216C and the MPU9250 on I2C2.
  *
  *  Both devices hang off I2C2 (PH4/PH5), the same bus as the PCF8574 I/O
  *  expander.  The expander is only touched once during start-up, so the
  *  sampler is the sole bus master at run time and needs no arbitration.
  *
  *  A sensor that is not fitted simply keeps its *_ok flag at 0 - the UI shows
  *  "--" for it instead of a stale value, and the failing bus is reported to
  *  the console the first time it happens (and then once every 30 s) instead of
  *  on every 500 ms round.
  ******************************************************************************
  */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_ap3216.h"
#include "bsp_mpu9250.h"

#include "sensor_task.h"

/* Re-attempt a device that failed once, at this interval (in sample rounds). */
#define RETRY_ROUNDS    4U      /* 4 x 500 ms = 2 s */

/* Minimum gap between two error reports, so a permanently absent device does
 * not flood the console (the UART TX ring buffer drops other logs when full). */
#define ERR_PRINT_ROUNDS  120U  /* 120 x 500 ms = 60 s */

static sensor_data_t s_data;
static uint32_t      s_round = 0U;
static uint32_t      s_ap_err_at  = 0U;    /* 0 = nothing reported yet       */
static uint32_t      s_mpu_err_at = 0U;
static uint16_t      s_ap_retry = 0U;
static uint16_t      s_mpu_retry = 0U;

/**
  * @brief  Rate-limited error report: prints at most once a minute per device.
  */
static void report_error(uint32_t *last_at, const char *fmt, int code)
{
    if ((*last_at == 0U) || ((s_round - *last_at) >= ERR_PRINT_ROUNDS))
    {
        *last_at = s_round;
        printf(fmt, code);
    }
}

void sensor_get(sensor_data_t *out)
{
    if (out == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    *out = s_data;
    taskEXIT_CRITICAL();
}

static void sample_ap3216(void)
{
    ap3216c_data_t d;

    if (s_ap_retry > 0U)
    {
        s_ap_retry--;
        return;
    }

    if (bsp_ap3216c_read(&d) != 0)
    {
        s_data.ap3216_ok = 0U;
        s_data.errors++;
        s_ap_retry = RETRY_ROUNDS;
        report_error(&s_ap_err_at,
                     "[SENS ] AP3216C read FAILED (I2C2, rc=%d), retry in 2s\r\n", -1);
        return;
    }

    s_data.ir  = d.ir;
    s_data.als = d.lux;
    s_data.ps  = d.ps;
    s_data.ap3216_ok = 1U;
}

static void sample_mpu9250(void)
{
    mpu9250_data_t d;
    int rc;

    if (s_mpu_retry > 0U)
    {
        s_mpu_retry--;
        return;
    }

    rc = bsp_mpu9250_read(&d);

    /* -3 means only the AK8963 magnetometer (behind the MPU's I2C master)
     * failed; the accel/gyro registers were read fine, so publish those
     * instead of throwing away the whole sample. */
    if ((rc == -1) || (rc == -2))
    {
        s_data.mpu_ok = 0U;
        s_data.mag_ok = 0U;
        s_data.errors++;
        s_mpu_retry = RETRY_ROUNDS;
        report_error(&s_mpu_err_at,
                     "[SENS ] MPU9250 read FAILED (I2C2, rc=%d), retry in 2s\r\n", rc);
        return;
    }

    s_data.ax = d.ax; s_data.ay = d.ay; s_data.az = d.az;
    s_data.gx = d.gx; s_data.gy = d.gy; s_data.gz = d.gz;
    s_data.mpu_ok = 1U;

    if (rc == 0)
    {
        s_data.mx = d.mx; s_data.my = d.my; s_data.mz = d.mz;
        s_data.mag_ok = 1U;
    }
    else
    {
        s_data.mag_ok = 0U;
        s_data.errors++;
        report_error(&s_mpu_err_at,
                     "[SENS ] AK8963 magnetometer read FAILED (rc=%d), "
                     "accel/gyro still published\r\n", rc);
    }
}

void sensor_task(void *arg)
{
    (void)arg;
    int ap_init  = -1;
    int mpu_init = -1;

    printf("[SENS ] task started, probing I2C2 devices\r\n");

    ap_init = bsp_ap3216c_init();
    printf("[SENS ] AP3216C init %s\r\n", (ap_init == 0) ? "OK" : "FAILED");

    mpu_init = bsp_mpu9250_init();
    printf("[SENS ] MPU9250 init %s (%s)\r\n",
           (mpu_init == 0) ? "OK" : "FAILED",
           (mpu_init == -2) ? "AK8963 unreachable" : "WHO_AM_I check");

    for (;;)
    {
        s_round++;

        if (ap_init == 0)
        {
            sample_ap3216();
        }
        if (mpu_init == 0)
        {
            sample_mpu9250();
        }

        s_data.samples = s_round;

        vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_MS));
    }
}
