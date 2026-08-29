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
  *  the console once and then at a low rate instead of on every 500 ms round.
  *
  *  WHY THIS TASK NEEDS THE EXTRA ARMOUR
  *  ------------------------------------
  *  HAL_I2C_Mem_Read/Write are POLLING transfers: the CPU has to stay in the
  *  driver loop to move every byte.  This task runs at idle+1 while the touch
  *  task runs at idle+4 and bit-bangs the GT9xx in ~1 ms bursts, so a transfer
  *  can be preempted mid-byte.  Three things keep that from becoming a
  *  permanent failure:
  *
  *    1. ATOMIC READS      - each sampling call is wrapped in
  *                           vTaskSuspendAll()/xTaskResumeAll(), so no other
  *                           task can preempt us mid-transfer.  Interrupts stay
  *                           enabled, so USB and the 1 ms tick are unaffected.
  *    2. RECOVERY ON ERROR - a timeout leaves the slave holding SDA low and the
  *                           I2C2 peripheral latched BUSY.  Without
  *                           BSP_I2C_Recover() every later read fails forever;
  *                           this is exactly the "works at init, then dies"
  *                           symptom.  We recover the bus before retrying.
  *    3. IMMEDIATE RETRY   - one extra attempt right after recovery, so a
  *                           single preemption hiccup never shows up as a
  *                           failed sample.
  ******************************************************************************
  */
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "bsp_ap3216.h"
#include "bsp_mpu9250.h"
#include "bsp_i2c.h"

#include "sensor_task.h"
#include "log.h"

/* Re-attempt a device that failed, at this interval (in sample rounds). */
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
static uint8_t       s_mag_present = 0U;   /* AK8963 answered during init    */
static uint32_t      s_recoveries = 0U;

/**
  * @brief  Rate-limited error report: prints at most once a minute per device.
  */
static void report_error(uint32_t *last_at, const char *fmt, int code)
{
    if ((*last_at == 0U) || ((s_round - *last_at) >= ERR_PRINT_ROUNDS))
    {
        *last_at = s_round;
        PRINT_LOG(fmt, code);
    }
}

/**
  * @brief  Un-wedge the I2C2 bus after a failed transfer.
  *
  *  A HAL timeout does not just drop one sample: the slave is left mid-byte
  *  holding SDA low and the peripheral keeps SR2.BUSY, so every following
  *  transfer fails instantly.  BSP_I2C_Recover() clocks the slave out,
  *  generates a STOP and re-initialises the peripheral.
  */
static void recover_bus(void)
{
    if (BSP_I2C_Recover() == 0)
    {
        s_recoveries++;
        PRINT_LOG("[SENS ] I2C2 bus recovered (count=%lu)\r\n",
                  (unsigned long)s_recoveries);
    }
    else
    {
        PRINT_LOG("[SENS ] I2C2 bus recovery FAILED (SDA still low?)\r\n");
    }
}

/**
  * @brief  Run one sampling call with the scheduler suspended.
  *
  *  The sampler runs below the touch task, so without this a bit-bang burst
  *  can land in the middle of a polled I2C byte.  Suspending the scheduler
  *  (not the interrupts) makes the transfer atomic; the longest call here is
  *  the MPU9250 read at ~15 ms, once every 500 ms.
  */
static int atomic_call(int (*fn)(void *), void *arg)
{
    int rc;

    vTaskSuspendAll();
    rc = fn(arg);
    xTaskResumeAll();

    return rc;
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

/* --- wrappers so atomic_call() has a uniform signature --------------------- */
static int do_ap_read(void *arg)
{
    return bsp_ap3216c_read((ap3216c_data_t *)arg);
}

static int do_mpu_read(void *arg)
{
    return bsp_mpu9250_read((mpu9250_data_t *)arg);
}

static void sample_ap3216(void)
{
    ap3216c_data_t d;
    int rc;

    if (s_ap_retry > 0U)
    {
        s_ap_retry--;
        return;
    }

    rc = atomic_call(do_ap_read, &d);

    /* One immediate retry after recovering the bus: a single preemption
     * hiccup should never become a visible failed sample. */
    if (rc != 0)
    {
        recover_bus();
        rc = atomic_call(do_ap_read, &d);
    }

    if (rc != 0)
    {
        s_data.ap3216_ok = 0U;
        s_data.errors++;
        s_ap_retry = RETRY_ROUNDS;
        report_error(&s_ap_err_at,
                     "[SENS ] AP3216C read FAILED (I2C2, rc=%d), retry in 2s\r\n", rc);
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

    rc = atomic_call(do_mpu_read, &d);

    if ((rc == -1) || (rc == -2))
    {
        recover_bus();
        rc = atomic_call(do_mpu_read, &d);
    }

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

    if (s_mag_present == 0U)
    {
        /* No AK8963 on this module: say so instead of publishing 0.0 uT. */
        s_data.mag_ok = 0U;
        s_data.mx = 0.0f; s_data.my = 0.0f; s_data.mz = 0.0f;
    }
    else if (rc == 0)
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

    PRINT_LOG("[SENS ] task started, probing I2C2 devices\r\n");

    ap_init = bsp_ap3216c_init();
    PRINT_LOG("[SENS ] AP3216C init %s\r\n", (ap_init == 0) ? "OK" : "FAILED");

    mpu_init = bsp_mpu9250_init();
    PRINT_LOG("[SENS ] MPU9250 init %s (rc=%d, AK8963 WIA=0x%02X)\r\n",
           (mpu_init == 0) ? "OK" : ((mpu_init == -3) ? "OK (no magnetometer)"
                                                      : "FAILED"),
           mpu_init, (unsigned int)bsp_mpu9250_mag_id());

    /* -3 means the AK8963 does not answer: the module carries no usable
     * magnetometer.  Accel/gyro are fine, so keep sampling and let the UI show
     * "not fitted" for the field instead of a fake 0.0 uT. */
    s_mag_present = (mpu_init == 0) ? 1U : 0U;

    for (;;)
    {
        s_round++;

        if (ap_init == 0)
        {
            sample_ap3216();
        }
        /* -3 also means accel/gyro are usable, just without the magnetometer. */
        if ((mpu_init == 0) || (mpu_init == -3))
        {
            sample_mpu9250();
        }

        s_data.samples = s_round;

        vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_MS));
    }
}
